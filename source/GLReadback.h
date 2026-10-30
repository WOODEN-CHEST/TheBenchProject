#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"

/*
 * Private header (source/): reading a small render target back to the CPU without waiting for the GPU, one of the
 * modules of direct OpenGL calls (BUILDING.md, "Direct OpenGL calls"; GLTextureArray.h, GLDepth.h, GLSliceMap.h and
 * GLShadowArray.h are the others).
 * raylib reads a texture back only by waiting until the GPU has finished everything before it (rlReadTexturePixels),
 * which would stall every frame. This copies a target into a pixel-pack buffer, fences the copy, and hands its values
 * over once the fence has passed, a frame or two later: the eye's metering (EyeAdaptation.h) needs the picture's
 * brightness, not at once.
 *
 * WHY HERE. Everything else draws through raylib. These calls are kept in this module, their function pointers resolved
 * through raylib's rlGetProcAddress (raylib is not rebuilt, no GL header or loader is added), their constants defined in
 * GLReadback.c. All of them are OpenGL 3.2 core (the sync objects) or older, within raylib's GL 3.3.
 *
 * USE. GLReadback_LoadApi once, after the GL context exists; a missing function leaves the API unavailable (the caller
 * logs it once and goes without). GLReadback_Construct for a target's size (one float a texel: GL_RED, GL_FLOAT), then
 * each frame GLReadback_Poll for the newest copy the GPU has finished and GLReadback_Request after drawing the target
 * (outside any BeginTextureMode). Main thread (the GL thread) only. A request binds the target's framebuffer and the
 * pixel-pack buffer target (which raylib never uses) and leaves both unbound, rlgl's defaults; the pack alignment is
 * restored.
 */


// Macros.
/* GL's calling convention through pointers: __stdcall on Windows (which matters for 32-bit builds), the default
 * elsewhere (GLTextureArray.h has the same). */
#if defined(_WIN32)
#define GL_READBACK_APIENTRY __stdcall
#else
#define GL_READBACK_APIENTRY
#endif
/** @brief How many copies may be in flight at once: the GPU is a frame or two behind the CPU, so three rarely wait for a
 *  free one (a request finding none is skipped, never waited for). */
#define GL_READBACK_SLOTS 3


// Types.
/* The GL functions' types, as GL declares them (int, unsigned int, ptrdiff_t and intptr_t are GL's GLsizei and GLint,
 * GLenum and GLuint, GLsizeiptr and GLintptr; a sync object is an opaque pointer, its timeout a 64-bit count). */
typedef void (GL_READBACK_APIENTRY* GLReadbackGenBuffersFunction)(int count, unsigned int* buffers);
typedef void (GL_READBACK_APIENTRY* GLReadbackDeleteBuffersFunction)(int count, const unsigned int* buffers);
typedef void (GL_READBACK_APIENTRY* GLReadbackBindBufferFunction)(unsigned int target, unsigned int buffer);
typedef void (GL_READBACK_APIENTRY* GLReadbackBufferDataFunction)(unsigned int target, ptrdiff_t size, const void* data,
    unsigned int usage);
typedef void (GL_READBACK_APIENTRY* GLReadbackGetBufferSubDataFunction)(unsigned int target, intptr_t offset,
    ptrdiff_t size, void* data);
typedef void (GL_READBACK_APIENTRY* GLReadbackReadPixelsFunction)(int x, int y, int width, int height,
    unsigned int format, unsigned int type, void* pixels);
typedef void (GL_READBACK_APIENTRY* GLReadbackPixelStoreiFunction)(unsigned int name, int value);
typedef void (GL_READBACK_APIENTRY* GLReadbackGetIntegervFunction)(unsigned int name, int* values);
typedef void* (GL_READBACK_APIENTRY* GLReadbackFenceSyncFunction)(unsigned int condition, unsigned int flags);
typedef unsigned int (GL_READBACK_APIENTRY* GLReadbackClientWaitSyncFunction)(void* sync, unsigned int flags,
    uint64_t timeout);
typedef void (GL_READBACK_APIENTRY* GLReadbackDeleteSyncFunction)(void* sync);
typedef unsigned int (GL_READBACK_APIENTRY* GLReadbackGetErrorFunction)(void);

/**
 * @brief The GL functions a readback needs, resolved through raylib's rlGetProcAddress. A plain value: fill it with
 *        GLReadback_LoadApi; nothing to release. Read-only after that.
 */
typedef struct GLReadbackApiStruct
{
    /** @brief Whether every function was found (the pointers are valid only then). */
    bool _isAvailable;
    /** @brief The first function that was missing (a static string), for the caller's one log line; NULL when available. */
    const char* _missing;
    GLReadbackGenBuffersFunction _genBuffers;
    GLReadbackDeleteBuffersFunction _deleteBuffers;
    GLReadbackBindBufferFunction _bindBuffer;
    GLReadbackBufferDataFunction _bufferData;
    GLReadbackGetBufferSubDataFunction _getBufferSubData;
    GLReadbackReadPixelsFunction _readPixels;
    GLReadbackPixelStoreiFunction _pixelStorei;
    GLReadbackGetIntegervFunction _getIntegerv;
    GLReadbackFenceSyncFunction _fenceSync;
    GLReadbackClientWaitSyncFunction _clientWaitSync;
    GLReadbackDeleteSyncFunction _deleteSync;
    GLReadbackGetErrorFunction _getError;
} GLReadbackApi;

/**
 * @brief Copies of one target's colour (one float a texel) on their way to the CPU: a pixel-pack buffer for each slot,
 *        the fences of the copies in flight, oldest first, in a ring. Construct it with GLReadback_Construct, release it
 *        with GLReadback_Deconstruct.
 */
typedef struct GLReadbackStruct
{
    /** @brief The API (the caller's, outliving this); NULL while not constructed. */
    const GLReadbackApi* _api;
    /** @brief The slots' pixel-pack buffers, and each slot's fence while its copy is in flight (NULL otherwise). */
    unsigned int _buffers[GL_READBACK_SLOTS];
    void* _fences[GL_READBACK_SLOTS];
    /** @brief The slot of the oldest copy in flight, and how many are in flight (the next request takes the slot after
     *  the newest). */
    int _first;
    int _count;
    /** @brief The target's size in texels. */
    int _width;
    int _height;
} GLReadback;


// Functions.
/**
 * @brief Resolves the GL functions through rlGetProcAddress. Call once, with raylib's GL context current.
 * @param outApi [out] The API; _isAvailable false (and _missing the first function not found) when one is missing.
 * @returns Whether the API is available.
 */
bool GLReadback_LoadApi(GLReadbackApi* outApi);

/**
 * @brief Makes the slots' buffers for copies of a target of @p width x @p height texels, one float each.
 * @param self The readback (overwritten); must not be NULL.
 * @param api The API; must not be NULL and must outlive @p self.
 * @param width The target's width in texels (1 to 4096).
 * @param height Its height in texels (1 to 4096).
 * @returns Success; ErrorCode_InvalidState when the API is unavailable or GL refused the buffers (its error code in the
 *          message; nothing is kept), ErrorCode_ArgumentOutOfRange for a size outside the limits. On failure @p self is
 *          left empty (GLReadback_Deconstruct is still safe, GLReadback_Request and GLReadback_Poll do nothing).
 */
Error GLReadback_Construct(GLReadback* self, const GLReadbackApi* api, int width, int height);

/**
 * @brief Frees the buffers and the fences of any copies in flight. Safe on a readback that failed to construct, and
 *        twice.
 * @param self The readback; must not be NULL.
 */
void GLReadback_Deconstruct(GLReadback* self);

/**
 * @brief Asks for a copy of a framebuffer's colour into the next free slot: its first colour attachment, which must hold
 *        at least the readback's size from its corner, read as one float a texel (the red channel). Skipped when every
 *        slot's copy is still in flight (the GPU is three requests behind): nothing waits.
 * @param self The readback; must not be NULL.
 * @param framebuffer The framebuffer to read (a RenderTexture2D's id; not 0).
 * @returns Whether a copy was asked for (false also when @p self is empty or @p framebuffer is 0).
 */
bool GLReadback_Request(GLReadback* self, unsigned int framebuffer);

/**
 * @brief Hands over the newest copy the GPU has finished, if any, and frees the slots of every finished one (an older
 *        finished copy is dropped). Never waits.
 * @param self The readback; must not be NULL.
 * @param outValues [out] Receives width x height floats, row by row from the bottom row (GL's order), when a copy was
 *        ready; untouched otherwise. Must hold that many.
 * @returns Whether a copy was ready.
 */
bool GLReadback_Poll(GLReadback* self, float* outValues);
