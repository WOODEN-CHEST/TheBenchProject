#include "GLSliceMap.h"
#include "raylib/rlgl.h"
#include "wr/WRMemory.h"

/*
 * Slice maps through the GL functions raylib's loader resolves (GLSliceMap.h): direct OpenGL calls, as GLDepth.c's.
 */


// Macros.
/* The GL enumerants used here (the OpenGL 3.3 core specification's values). */
#define GL_TEXTURE_2D 0x0DE1U
#define GL_TEXTURE_MIN_FILTER 0x2801U
#define GL_TEXTURE_MAG_FILTER 0x2800U
#define GL_TEXTURE_WRAP_S 0x2802U
#define GL_TEXTURE_WRAP_T 0x2803U
#define GL_TEXTURE_BASE_LEVEL 0x813CU
#define GL_TEXTURE_MAX_LEVEL 0x813DU
#define GL_NEAREST 0x2600
#define GL_NEAREST_MIPMAP_NEAREST 0x2700
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_RGBA32UI 0x8D70
#define GL_RGBA_INTEGER 0x8D99U
#define GL_UNSIGNED_INT 0x1405U
#define GL_COLOR_LOGIC_OP 0x0BF2U
#define GL_XOR 0x1506U
#define GL_OR 0x1507U
#define GL_COLOR 0x1800U
#define GL_NO_ERROR 0U
#define GL_OUT_OF_MEMORY 0x0505U
/** The largest side a slice map may have here. */
#define MAX_SIDE 8192
/** How many queued GL errors are read off before a creation (a lost context could report one forever). */
#define MAX_DRAINED_ERRORS 32


// Types.
/* Reads a loader's object pointer as a function pointer: ISO C has no cast between the two, a union reads the same bytes
 * (every target platform's function pointers are object-pointer sized: checked below). */
typedef union GLSliceMapAddressUnion
{
    void* Object;
    GLSliceMapGenTexturesFunction GenTextures;
    GLSliceMapDeleteTexturesFunction DeleteTextures;
    GLSliceMapBindTextureFunction BindTexture;
    GLSliceMapTexImage2DFunction TexImage2D;
    GLSliceMapTexParameteriFunction TexParameteri;
    GLSliceMapEnableFunction Enable;
    GLSliceMapLogicOpFunction LogicOp;
    GLSliceMapClearBufferuivFunction ClearBufferuiv;
    GLSliceMapGetErrorFunction GetError;
} GLSliceMapAddress;

_Static_assert(sizeof(void*) == sizeof(GLSliceMapGetErrorFunction), "GL functions are read through object pointers");


// Static functions.
/* One function's address through raylib's loader; the first one missing is remembered for the message. */
static GLSliceMapAddress FindFunction(const char* name, const char** ioMissing)
{
    GLSliceMapAddress Address = { .Object = rlGetProcAddress(name) };
    if ((Address.Object == NULL) && (*ioMissing == NULL))
    {
        *ioMissing = name;
    }
    return Address;
}

/* Reads off errors queued by earlier calls, so the next check sees only its own. */
static void DrainErrors(const GLSliceMapApi* api)
{
    for (int Count = 0; (Count < MAX_DRAINED_ERRORS) && (api->_getError() != GL_NO_ERROR); Count++)
    {
    }
}

/* How many levels a side of @p size halves to 1 in (1 for a side of 1). */
static int GetMaxLevelCount(int size)
{
    int Count = 1;
    for (int Side = size; Side > 1; Side /= 2)
    {
        Count++;
    }
    return Count;
}

/* Sets a slice map's readable levels, from base to highest; leaves no texture bound on unit 0. */
static void SetReadLevels(const GLSliceMapApi* api, unsigned int id, int base, int highest)
{
    rlActiveTextureSlot(0);
    api->_bindTexture(GL_TEXTURE_2D, id);
    api->_texParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, base);
    api->_texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, highest);
    api->_bindTexture(GL_TEXTURE_2D, 0U);
}


// Internal functions (declared in GLSliceMap.h).
bool GLSliceMap_LoadApi(GLSliceMapApi* outApi)
{
    Memory_Zero(outApi, sizeof(*outApi));
    const char* Missing = NULL;
    outApi->_genTextures = FindFunction("glGenTextures", &Missing).GenTextures;
    outApi->_deleteTextures = FindFunction("glDeleteTextures", &Missing).DeleteTextures;
    outApi->_bindTexture = FindFunction("glBindTexture", &Missing).BindTexture;
    outApi->_texImage2D = FindFunction("glTexImage2D", &Missing).TexImage2D;
    outApi->_texParameteri = FindFunction("glTexParameteri", &Missing).TexParameteri;
    outApi->_enable = FindFunction("glEnable", &Missing).Enable;
    outApi->_disable = FindFunction("glDisable", &Missing).Enable;
    outApi->_logicOp = FindFunction("glLogicOp", &Missing).LogicOp;
    outApi->_clearBufferuiv = FindFunction("glClearBufferuiv", &Missing).ClearBufferuiv;
    outApi->_getError = FindFunction("glGetError", &Missing).GetError;
    outApi->_missing = Missing;
    outApi->_isAvailable = Missing == NULL;
    return outApi->_isAvailable;
}

Error GLSliceMap_CreateTexture(const GLSliceMapApi* api, int size, int levelCount, unsigned int* outId)
{
    *outId = 0U;
    if (!api->_isAvailable)
    {
        return Error_Construct4(ErrorCode_InvalidState, "Slice maps are unavailable (%s).",
            (api->_missing != NULL) ? api->_missing : "a GL function");
    }
    if ((size < 1) || (size > MAX_SIDE) || (levelCount < 1) || (levelCount > GetMaxLevelCount(size)))
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange, "A slice map of %d texels a side with %d levels is outside "
            "the limits (1 to %d a side, as many levels as it halves in).", size, levelCount, MAX_SIDE);
    }
    // Unit 0's 2D target, left unbound after: raylib binds its own textures per draw.
    rlActiveTextureSlot(0);
    DrainErrors(api);
    unsigned int Id = 0U;
    api->_genTextures(1, &Id);
    api->_bindTexture(GL_TEXTURE_2D, Id);
    int Side = size;
    for (int Level = 0; Level < levelCount; Level++)
    {
        api->_texImage2D(GL_TEXTURE_2D, Level, GL_RGBA32UI, Side, Side, 0, GL_RGBA_INTEGER, GL_UNSIGNED_INT, NULL);
        Side = (Side > 1) ? (Side / 2) : 1;
    }
    // A fetch from a level other than the base needs a mipmapped minification filter (GL 3.3, "Texel Fetches").
    api->_texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (levelCount > 1) ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST);
    api->_texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    api->_texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    api->_texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    api->_texParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    api->_texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levelCount - 1);
    unsigned int Status = api->_getError();
    api->_bindTexture(GL_TEXTURE_2D, 0U);
    if (Status != GL_NO_ERROR)
    {
        api->_deleteTextures(1, &Id);
        if (Status == GL_OUT_OF_MEMORY)
        {
            return Error_Construct4(ErrorCode_InvalidState, "The GPU has no room for a slice map of %d texels a side.",
                size);
        }
        return Error_Construct4(ErrorCode_InvalidState, "GL refused a slice map of %d texels a side (error 0x%04X).", size,
            Status);
    }
    *outId = Id;
    return Error_CreateSuccess();
}

void GLSliceMap_DeleteTexture(const GLSliceMapApi* api, unsigned int id)
{
    if (id != 0U)
    {
        api->_deleteTextures(1, &id);
    }
}

void GLSliceMap_Clear(const GLSliceMapApi* api)
{
    rlDrawRenderBatchActive();
    const unsigned int Zero[4] = { 0U, 0U, 0U, 0U };
    api->_clearBufferuiv(GL_COLOR, 0, Zero);
}

void GLSliceMap_BeginLogicOp(const GLSliceMapApi* api, GLSliceMapOp op)
{
    rlDrawRenderBatchActive();
    api->_enable(GL_COLOR_LOGIC_OP);
    api->_logicOp((op == GLSliceMapOp_Xor) ? GL_XOR : GL_OR);
}

void GLSliceMap_EndLogicOp(const GLSliceMapApi* api)
{
    rlDrawRenderBatchActive();
    api->_disable(GL_COLOR_LOGIC_OP);
}

void GLSliceMap_ReadLevel(const GLSliceMapApi* api, unsigned int id, int level)
{
    SetReadLevels(api, id, level, level);
}

void GLSliceMap_ReadAllLevels(const GLSliceMapApi* api, unsigned int id, int levelCount)
{
    SetReadLevels(api, id, 0, levelCount - 1);
}
