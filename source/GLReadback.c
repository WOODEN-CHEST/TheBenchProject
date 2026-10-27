#include "GLReadback.h"
#include "raylib/rlgl.h"
#include "wr/WRMemory.h"

/*
 * Copies of small targets to the CPU through pixel-pack buffers and fences (GLReadback.h): direct OpenGL calls, as
 * GLTextureArray.c's.
 */


// Macros.
/* The GL enumerants used here (the OpenGL 3.3 core specification's values). */
#define GL_PIXEL_PACK_BUFFER 0x88EBU
#define GL_STREAM_READ 0x88E1U
#define GL_RED 0x1903U
#define GL_FLOAT 0x1406U
#define GL_PACK_ALIGNMENT 0x0D05U
#define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117U
#define GL_SYNC_FLUSH_COMMANDS_BIT 0x00000001U
#define GL_ALREADY_SIGNALED 0x911AU
#define GL_CONDITION_SATISFIED 0x911CU
#define GL_TIMEOUT_EXPIRED 0x911BU
#define GL_NO_ERROR 0U
#define GL_OUT_OF_MEMORY 0x0505U
/** The pack alignment of a copy: a float texel is 4 bytes, so every row is tightly packed at 4 (at 8 an odd width's rows
 *  would be padded). */
#define PACK_ALIGNMENT 4
/** The largest side Construct accepts (a copy is at most 64 MiB). */
#define MAX_SIDE 4096
/** How many queued GL errors are read off before a construction (a lost context could report one forever). */
#define MAX_DRAINED_ERRORS 32


// Types.
/* Reads a loader's object pointer as a function pointer: ISO C has no cast between the two, a union reads the same bytes
 * (every target platform's function pointers are object-pointer sized: checked below). */
typedef union GLReadbackAddressUnion
{
    void* Object;
    GLReadbackGenBuffersFunction GenBuffers;
    GLReadbackDeleteBuffersFunction DeleteBuffers;
    GLReadbackBindBufferFunction BindBuffer;
    GLReadbackBufferDataFunction BufferData;
    GLReadbackGetBufferSubDataFunction GetBufferSubData;
    GLReadbackReadPixelsFunction ReadPixels;
    GLReadbackPixelStoreiFunction PixelStorei;
    GLReadbackGetIntegervFunction GetIntegerv;
    GLReadbackFenceSyncFunction FenceSync;
    GLReadbackClientWaitSyncFunction ClientWaitSync;
    GLReadbackDeleteSyncFunction DeleteSync;
    GLReadbackGetErrorFunction GetError;
} GLReadbackAddress;

_Static_assert(sizeof(void*) == sizeof(GLReadbackGetErrorFunction), "GL functions are read through object pointers");


// Static functions.
/* One function's address through raylib's loader; the first one missing is remembered for the message. */
static GLReadbackAddress FindFunction(const char* name, const char** ioMissing)
{
    GLReadbackAddress Address = { .Object = rlGetProcAddress(name) };
    if ((Address.Object == NULL) && (*ioMissing == NULL))
    {
        *ioMissing = name;
    }
    return Address;
}

/* Reads off errors queued by earlier calls, so the next check sees only its own. */
static void DrainErrors(const GLReadbackApi* api)
{
    for (int Count = 0; (Count < MAX_DRAINED_ERRORS) && (api->_getError() != GL_NO_ERROR); Count++)
    {
    }
}

/* Frees the oldest copy in flight's slot (its fence). */
static void FreeOldest(GLReadback* self)
{
    self->_api->_deleteSync(self->_fences[self->_first]);
    self->_fences[self->_first] = NULL;
    self->_first = (self->_first + 1) % GL_READBACK_SLOTS;
    self->_count--;
}


// Internal functions (declared in GLReadback.h).
bool GLReadback_LoadApi(GLReadbackApi* outApi)
{
    Memory_Zero(outApi, sizeof(*outApi));
    const char* Missing = NULL;
    outApi->_genBuffers = FindFunction("glGenBuffers", &Missing).GenBuffers;
    outApi->_deleteBuffers = FindFunction("glDeleteBuffers", &Missing).DeleteBuffers;
    outApi->_bindBuffer = FindFunction("glBindBuffer", &Missing).BindBuffer;
    outApi->_bufferData = FindFunction("glBufferData", &Missing).BufferData;
    outApi->_getBufferSubData = FindFunction("glGetBufferSubData", &Missing).GetBufferSubData;
    outApi->_readPixels = FindFunction("glReadPixels", &Missing).ReadPixels;
    outApi->_pixelStorei = FindFunction("glPixelStorei", &Missing).PixelStorei;
    outApi->_getIntegerv = FindFunction("glGetIntegerv", &Missing).GetIntegerv;
    outApi->_fenceSync = FindFunction("glFenceSync", &Missing).FenceSync;
    outApi->_clientWaitSync = FindFunction("glClientWaitSync", &Missing).ClientWaitSync;
    outApi->_deleteSync = FindFunction("glDeleteSync", &Missing).DeleteSync;
    outApi->_getError = FindFunction("glGetError", &Missing).GetError;
    outApi->_missing = Missing;
    outApi->_isAvailable = Missing == NULL;
    return outApi->_isAvailable;
}

Error GLReadback_Construct(GLReadback* self, const GLReadbackApi* api, int width, int height)
{
    Memory_Zero(self, sizeof(*self));
    if (!api->_isAvailable)
    {
        return Error_Construct4(ErrorCode_InvalidState, "Readbacks are unavailable (%s is missing).",
            (api->_missing != NULL) ? api->_missing : "a GL function");
    }
    if ((width < 1) || (height < 1) || (width > MAX_SIDE) || (height > MAX_SIDE))
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange, "A readback of %d x %d texels is outside the limits (1 to "
            "%d per side).", width, height, MAX_SIDE);
    }
    DrainErrors(api);
    unsigned int Buffers[GL_READBACK_SLOTS] = { 0U };
    api->_genBuffers(GL_READBACK_SLOTS, Buffers);
    ptrdiff_t ByteCount = (ptrdiff_t)width * (ptrdiff_t)height * (ptrdiff_t)sizeof(float);
    for (int Slot = 0; Slot < GL_READBACK_SLOTS; Slot++)
    {
        api->_bindBuffer(GL_PIXEL_PACK_BUFFER, Buffers[Slot]);
        api->_bufferData(GL_PIXEL_PACK_BUFFER, ByteCount, NULL, GL_STREAM_READ);
    }
    api->_bindBuffer(GL_PIXEL_PACK_BUFFER, 0U);
    unsigned int Status = api->_getError();
    if (Status != GL_NO_ERROR)
    {
        api->_deleteBuffers(GL_READBACK_SLOTS, Buffers);
        if (Status == GL_OUT_OF_MEMORY)
        {
            return Error_Construct4(ErrorCode_InvalidState, "The GPU has no room for a readback of %d x %d texels.",
                width, height);
        }
        return Error_Construct4(ErrorCode_InvalidState, "GL refused a readback of %d x %d texels (error 0x%04X).", width,
            height, Status);
    }
    self->_api = api;
    Memory_Copy(Buffers, self->_buffers, sizeof(Buffers));
    self->_width = width;
    self->_height = height;
    return Error_CreateSuccess();
}

void GLReadback_Deconstruct(GLReadback* self)
{
    if (self->_api != NULL)
    {
        while (self->_count > 0)
        {
            FreeOldest(self);
        }
        self->_api->_deleteBuffers(GL_READBACK_SLOTS, self->_buffers);
    }
    Memory_Zero(self, sizeof(*self));
}

bool GLReadback_Request(GLReadback* self, unsigned int framebuffer)
{
    if ((self->_api == NULL) || (framebuffer == 0U) || (self->_count >= GL_READBACK_SLOTS))
    {
        return false;
    }
    const GLReadbackApi* Api = self->_api;
    int Slot = (self->_first + self->_count) % GL_READBACK_SLOTS;
    // Whatever the batch still holds belongs to the framebuffer bound before this one.
    rlDrawRenderBatchActive();
    rlEnableFramebuffer(framebuffer);
    int PreviousAlignment = PACK_ALIGNMENT;
    Api->_getIntegerv(GL_PACK_ALIGNMENT, &PreviousAlignment);
    Api->_pixelStorei(GL_PACK_ALIGNMENT, PACK_ALIGNMENT);
    Api->_bindBuffer(GL_PIXEL_PACK_BUFFER, self->_buffers[Slot]);
    // Into the bound buffer at offset 0: the copy runs on the GPU, in order with the frame's work.
    Api->_readPixels(0, 0, self->_width, self->_height, GL_RED, GL_FLOAT, NULL);
    Api->_bindBuffer(GL_PIXEL_PACK_BUFFER, 0U);
    Api->_pixelStorei(GL_PACK_ALIGNMENT, PreviousAlignment);
    rlDisableFramebuffer();
    void* Fence = Api->_fenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0U);
    if (Fence == NULL)
    {
        return false;
    }
    self->_fences[Slot] = Fence;
    self->_count++;
    return true;
}

bool GLReadback_Poll(GLReadback* self, float* outValues)
{
    if (self->_api == NULL)
    {
        return false;
    }
    const GLReadbackApi* Api = self->_api;
    // Fences pass in the order they were set, so the finished copies are the oldest ones: free them oldest first and keep
    // the newest. A timeout of 0 only asks; the flush sends a fence still queued on the CPU on its way.
    int Newest = -1;
    while (self->_count > 0)
    {
        unsigned int Status = Api->_clientWaitSync(self->_fences[self->_first], GL_SYNC_FLUSH_COMMANDS_BIT, 0U);
        if (Status == GL_TIMEOUT_EXPIRED)
        {
            break;
        }
        // Passed, or the wait failed (GL_WAIT_FAILED: that copy is dropped).
        if ((Status == GL_ALREADY_SIGNALED) || (Status == GL_CONDITION_SATISFIED))
        {
            Newest = self->_first;
        }
        FreeOldest(self);
    }
    if (Newest < 0)
    {
        return false;
    }
    Api->_bindBuffer(GL_PIXEL_PACK_BUFFER, self->_buffers[Newest]);
    Api->_getBufferSubData(GL_PIXEL_PACK_BUFFER, 0, (ptrdiff_t)self->_width * (ptrdiff_t)self->_height
        * (ptrdiff_t)sizeof(float), outValues);
    Api->_bindBuffer(GL_PIXEL_PACK_BUFFER, 0U);
    return true;
}
