#include <string.h>
#include "GLDepth.h"
#include "raylib/rlgl.h"
#include "wr/WRMemory.h"

/*
 * Reversed depth through the GL functions raylib's loader resolves (GLDepth.h): direct OpenGL calls, as
 * GLTextureArray.c's.
 */


// Macros.
/* The GL enumerants used here (the OpenGL 4.5 core specification's values; clip control's are ARB_clip_control's too). */
#define GL_LOWER_LEFT 0x8CA1U
#define GL_NEGATIVE_ONE_TO_ONE 0x935EU
#define GL_ZERO_TO_ONE 0x935FU
#define GL_LEQUAL 0x0203U
#define GL_GEQUAL 0x0206U
#define GL_TEXTURE_2D 0x0DE1U
#define GL_TEXTURE_MIN_FILTER 0x2801U
#define GL_TEXTURE_MAG_FILTER 0x2800U
#define GL_TEXTURE_WRAP_S 0x2802U
#define GL_TEXTURE_WRAP_T 0x2803U
#define GL_NEAREST 0x2600
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_DEPTH_COMPONENT 0x1902U
#define GL_DEPTH_COMPONENT32F 0x8CAC
#define GL_FLOAT 0x1406U
#define GL_RENDERBUFFER 0x8D41U
#define GL_MAJOR_VERSION 0x821BU
#define GL_MINOR_VERSION 0x821CU
#define GL_NUM_EXTENSIONS 0x821DU
#define GL_EXTENSIONS 0x1F03U
#define GL_NO_ERROR 0U
#define GL_OUT_OF_MEMORY 0x0505U
/** The extension that brings clip control to GPUs below OpenGL 4.5. */
#define CLIP_CONTROL_EXTENSION "GL_ARB_clip_control"
/** The largest side a depth buffer may have here. */
#define MAX_SIDE 16384
/** How many queued GL errors are read off before a creation (a lost context could report one forever). */
#define MAX_DRAINED_ERRORS 32


// Types.
/* Reads a loader's object pointer as a function pointer: ISO C has no cast between the two, a union reads the same bytes
 * (every target platform's function pointers are object-pointer sized: checked below). */
typedef union GLDepthAddressUnion
{
    void* Object;
    GLDepthClipControlFunction ClipControl;
    GLDepthDepthFuncFunction DepthFunc;
    GLDepthClearDepthFunction ClearDepth;
    GLDepthGenTexturesFunction GenTextures;
    GLDepthDeleteTexturesFunction DeleteTextures;
    GLDepthBindTextureFunction BindTexture;
    GLDepthTexImage2DFunction TexImage2D;
    GLDepthTexParameteriFunction TexParameteri;
    GLDepthGenRenderbuffersFunction GenRenderbuffers;
    GLDepthDeleteRenderbuffersFunction DeleteRenderbuffers;
    GLDepthBindRenderbufferFunction BindRenderbuffer;
    GLDepthRenderbufferStorageFunction RenderbufferStorage;
    GLDepthGetIntegervFunction GetIntegerv;
    GLDepthGetStringiFunction GetStringi;
    GLDepthGetErrorFunction GetError;
} GLDepthAddress;

_Static_assert(sizeof(void*) == sizeof(GLDepthGetErrorFunction), "GL functions are read through object pointers");


// Static functions.
/* One function's address through raylib's loader; the first one missing is remembered for the message. */
static GLDepthAddress FindFunction(const char* name, const char** ioMissing)
{
    GLDepthAddress Address = { .Object = rlGetProcAddress(name) };
    if ((Address.Object == NULL) && (*ioMissing == NULL))
    {
        *ioMissing = name;
    }
    return Address;
}

/* Reads off errors queued by earlier calls, so the next check sees only its own. */
static void DrainErrors(const GLDepthApi* api)
{
    for (int Count = 0; (Count < MAX_DRAINED_ERRORS) && (api->_getError() != GL_NO_ERROR); Count++)
    {
    }
}

/* Whether the context has clip control: OpenGL 4.5 or newer, or the extension. A loader may hand out a function a
 * context does not have, so the version decides, not the pointer. */
static bool HasClipControl(const GLDepthApi* api)
{
    int Major = 0;
    int Minor = 0;
    api->_getIntegerv(GL_MAJOR_VERSION, &Major);
    api->_getIntegerv(GL_MINOR_VERSION, &Minor);
    if ((Major > 4) || ((Major == 4) && (Minor >= 5)))
    {
        return true;
    }
    int Count = 0;
    api->_getIntegerv(GL_NUM_EXTENSIONS, &Count);
    for (int Index = 0; Index < Count; Index++)
    {
        const unsigned char* Name = api->_getStringi(GL_EXTENSIONS, (unsigned int)Index);
        if ((Name != NULL) && (strcmp((const char*)Name, CLIP_CONTROL_EXTENSION) == 0))
        {
            return true;
        }
    }
    return false;
}

/* The error for a GL failure while making a depth buffer of this size. */
static Error StorageError(unsigned int status, const char* what, int width, int height)
{
    if (status == GL_OUT_OF_MEMORY)
    {
        return Error_Construct4(ErrorCode_InvalidState, "The GPU has no room for a %s of %d x %d pixels.", what, width,
            height);
    }
    return Error_Construct4(ErrorCode_InvalidState, "GL refused a %s of %d x %d pixels (error 0x%04X).", what, width,
        height, status);
}

/* Checks what every creation needs: an available API and a size within the limits. */
static Error CheckCreation(const GLDepthApi* api, const char* what, int width, int height)
{
    if (!api->_isAvailable)
    {
        return Error_Construct4(ErrorCode_InvalidState, "Reversed depth is unavailable (%s).",
            (api->_missing != NULL) ? api->_missing : "a GL function");
    }
    if ((width < 1) || (height < 1) || (width > MAX_SIDE) || (height > MAX_SIDE))
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange, "A %s of %d x %d pixels is outside the limits (1 to %d "
            "per side).", what, width, height, MAX_SIDE);
    }
    return Error_CreateSuccess();
}


// Internal functions (declared in GLDepth.h).
bool GLDepth_LoadApi(GLDepthApi* outApi)
{
    Memory_Zero(outApi, sizeof(*outApi));
    const char* Missing = NULL;
    outApi->_clipControl = FindFunction("glClipControl", &Missing).ClipControl;
    outApi->_depthFunc = FindFunction("glDepthFunc", &Missing).DepthFunc;
    outApi->_clearDepth = FindFunction("glClearDepth", &Missing).ClearDepth;
    outApi->_genTextures = FindFunction("glGenTextures", &Missing).GenTextures;
    outApi->_deleteTextures = FindFunction("glDeleteTextures", &Missing).DeleteTextures;
    outApi->_bindTexture = FindFunction("glBindTexture", &Missing).BindTexture;
    outApi->_texImage2D = FindFunction("glTexImage2D", &Missing).TexImage2D;
    outApi->_texParameteri = FindFunction("glTexParameteri", &Missing).TexParameteri;
    outApi->_genRenderbuffers = FindFunction("glGenRenderbuffers", &Missing).GenRenderbuffers;
    outApi->_deleteRenderbuffers = FindFunction("glDeleteRenderbuffers", &Missing).DeleteRenderbuffers;
    outApi->_bindRenderbuffer = FindFunction("glBindRenderbuffer", &Missing).BindRenderbuffer;
    outApi->_renderbufferStorage = FindFunction("glRenderbufferStorage", &Missing).RenderbufferStorage;
    outApi->_getIntegerv = FindFunction("glGetIntegerv", &Missing).GetIntegerv;
    outApi->_getStringi = FindFunction("glGetStringi", &Missing).GetStringi;
    outApi->_getError = FindFunction("glGetError", &Missing).GetError;
    if (Missing == NULL)
    {
        DrainErrors(outApi);
        if (!HasClipControl(outApi))
        {
            Missing = "clip control: OpenGL 4.5 or " CLIP_CONTROL_EXTENSION;
        }
    }
    outApi->_missing = Missing;
    outApi->_isAvailable = Missing == NULL;
    return outApi->_isAvailable;
}

Error GLDepth_CreateTexture(const GLDepthApi* api, int width, int height, unsigned int* outId)
{
    *outId = 0U;
    Error Check = CheckCreation(api, "depth texture", width, height);
    if (Check.Code != ErrorCode_Success)
    {
        return Check;
    }
    // Unit 0's 2D target, left unbound after: raylib binds its own textures per draw.
    rlActiveTextureSlot(0);
    DrainErrors(api);
    unsigned int Id = 0U;
    api->_genTextures(1, &Id);
    api->_bindTexture(GL_TEXTURE_2D, Id);
    api->_texImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    api->_texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    api->_texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    api->_texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    api->_texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    unsigned int Status = api->_getError();
    api->_bindTexture(GL_TEXTURE_2D, 0U);
    if (Status != GL_NO_ERROR)
    {
        api->_deleteTextures(1, &Id);
        return StorageError(Status, "depth texture", width, height);
    }
    *outId = Id;
    return Error_CreateSuccess();
}

Error GLDepth_CreateRenderbuffer(const GLDepthApi* api, int width, int height, unsigned int* outId)
{
    *outId = 0U;
    Error Check = CheckCreation(api, "depth renderbuffer", width, height);
    if (Check.Code != ErrorCode_Success)
    {
        return Check;
    }
    DrainErrors(api);
    unsigned int Id = 0U;
    api->_genRenderbuffers(1, &Id);
    api->_bindRenderbuffer(GL_RENDERBUFFER, Id);
    api->_renderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT32F, width, height);
    unsigned int Status = api->_getError();
    api->_bindRenderbuffer(GL_RENDERBUFFER, 0U);
    if (Status != GL_NO_ERROR)
    {
        api->_deleteRenderbuffers(1, &Id);
        return StorageError(Status, "depth renderbuffer", width, height);
    }
    *outId = Id;
    return Error_CreateSuccess();
}

void GLDepth_BeginReversed(const GLDepthApi* api)
{
    rlDrawRenderBatchActive();
    api->_clipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
    api->_depthFunc(GL_GEQUAL);
    api->_clearDepth(0.0);
}

void GLDepth_EndReversed(const GLDepthApi* api)
{
    rlDrawRenderBatchActive();
    api->_clipControl(GL_LOWER_LEFT, GL_NEGATIVE_ONE_TO_ONE);
    api->_depthFunc(GL_LEQUAL);
    api->_clearDepth(1.0);
}
