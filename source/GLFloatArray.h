#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "wr/WRError.h"

/*
 * Private header (source/): float colour texture arrays drawn into a layer at a time, one of the modules of direct OpenGL
 * calls (BUILDING.md, "Direct OpenGL calls"; GLTextureArray.h, GLReadback.h, GLDepth.h, GLSliceMap.h, GLShadowArray.h
 * and GLTexture3D.h are the others).
 *
 * WHAT IT IS. A texture array of 32-bit floats, one or two channels a texel (GL_R32F, GL_RG32F), filtered linearly and
 * clamped to its edges, without mipmaps, and a framebuffer that takes one layer at a time as its colour, optionally with
 * a depth renderbuffer of the layers' size: the far shadows' height maps (one channel, drawn from above with the depth
 * test keeping the highest surface) and their sweeps' shadow heights (two channels: the light's disc's two edges), which
 * the lit programs read through one sampler2DArray whatever the number of maps. Values far outside a byte's range (a
 * height of a kilometre, "no ground" at -1e9) are cleared and kept exactly.
 *
 * WHY HERE. raylib has no texture arrays, no two-channel float format, no layer attachments and no float clear values.
 * These calls are kept in this module, their function pointers resolved through raylib's rlGetProcAddress (no raylib
 * rebuild, no GL header or loader added), their constants defined in GLFloatArray.c; every one is OpenGL 3.0 core, which
 * raylib's 3.3 already requires (desktop GL filters 32-bit float textures linearly). A missing function, or a GL error
 * after making the array (a loader may hand out a stub for a function the context lacks), leaves the array unmade: the
 * caller logs it once and draws without what needs it.
 *
 * USE. GLFloatArray_LoadApi once, after the GL context exists; GLFloatArray_Create for an array, GLFloatArray_Destroy to
 * release it. To draw into a layer: GLFloatArray_AttachLayer, then raylib's BeginTextureMode with a RenderTexture2D whose
 * id is GLFloatArray_GetFramebuffer and whose texture's size is the array's (it binds the framebuffer and sets the
 * viewport), GLFloatArray_Clear for float clear values, draw, EndTextureMode. To read it: GLFloatArray_Bind on a unit's
 * array target, for a sampler2DArray pointed there. Never read an array while one of its layers is attached to the
 * framebuffer being drawn (GL leaves that undefined, whatever the layer). Main thread (the GL thread) only.
 */


// Macros.
/* GL's calling convention through pointers: __stdcall on Windows (which matters for 32-bit builds), the default
 * elsewhere (as GLTextureArray.h's). */
#if defined(_WIN32)
#define GL_FLOAT_ARRAY_APIENTRY __stdcall
#else
#define GL_FLOAT_ARRAY_APIENTRY
#endif

/** @brief The largest side an array may have here, in texels. */
#define GL_FLOAT_ARRAY_MAX_SIZE 8192
/** @brief The most layers an array may have here. */
#define GL_FLOAT_ARRAY_MAX_LAYERS 16


// Types.
/* The GL functions' types, as GL declares them (int and unsigned int are GL's GLint and GLsizei, GLenum and GLuint). */
typedef void (GL_FLOAT_ARRAY_APIENTRY* GLFloatArrayGenFunction)(int count, unsigned int* names);
typedef void (GL_FLOAT_ARRAY_APIENTRY* GLFloatArrayDeleteFunction)(int count, const unsigned int* names);
typedef void (GL_FLOAT_ARRAY_APIENTRY* GLFloatArrayBindFunction)(unsigned int target, unsigned int name);
typedef void (GL_FLOAT_ARRAY_APIENTRY* GLFloatArrayTexImage3DFunction)(unsigned int target, int level,
    int internalFormat, int width, int height, int depth, int border, unsigned int format, unsigned int type,
    const void* pixels);
typedef void (GL_FLOAT_ARRAY_APIENTRY* GLFloatArrayTexParameteriFunction)(unsigned int target, unsigned int name,
    int value);
typedef void (GL_FLOAT_ARRAY_APIENTRY* GLFloatArrayFramebufferTextureLayerFunction)(unsigned int target,
    unsigned int attachment, unsigned int texture, int level, int layer);
typedef void (GL_FLOAT_ARRAY_APIENTRY* GLFloatArrayRenderbufferStorageFunction)(unsigned int target,
    unsigned int internalFormat, int width, int height);
typedef void (GL_FLOAT_ARRAY_APIENTRY* GLFloatArrayFramebufferRenderbufferFunction)(unsigned int target,
    unsigned int attachment, unsigned int renderbufferTarget, unsigned int renderbuffer);
typedef unsigned int (GL_FLOAT_ARRAY_APIENTRY* GLFloatArrayCheckFramebufferStatusFunction)(unsigned int target);
typedef void (GL_FLOAT_ARRAY_APIENTRY* GLFloatArrayClearBufferfvFunction)(unsigned int buffer, int drawBuffer,
    const float* value);
typedef void (GL_FLOAT_ARRAY_APIENTRY* GLFloatArrayGetIntegervFunction)(unsigned int name, int* values);
typedef unsigned int (GL_FLOAT_ARRAY_APIENTRY* GLFloatArrayGetErrorFunction)(void);

/**
 * @brief The GL functions the float arrays need, resolved through raylib's rlGetProcAddress. A plain value: fill it with
 *        GLFloatArray_LoadApi; nothing to release. Read-only after that.
 */
typedef struct GLFloatArrayApiStruct
{
    /** @brief Whether every function was found (the pointers are valid only then). */
    bool _isAvailable;
    /** @brief The first function missing, for the caller's one log line (a static string); NULL when available. */
    const char* _missing;
    GLFloatArrayGenFunction _genTextures;
    GLFloatArrayDeleteFunction _deleteTextures;
    GLFloatArrayBindFunction _bindTexture;
    GLFloatArrayTexImage3DFunction _texImage3D;
    GLFloatArrayTexParameteriFunction _texParameteri;
    GLFloatArrayGenFunction _genFramebuffers;
    GLFloatArrayDeleteFunction _deleteFramebuffers;
    GLFloatArrayBindFunction _bindFramebuffer;
    GLFloatArrayFramebufferTextureLayerFunction _framebufferTextureLayer;
    GLFloatArrayGenFunction _genRenderbuffers;
    GLFloatArrayDeleteFunction _deleteRenderbuffers;
    GLFloatArrayBindFunction _bindRenderbuffer;
    GLFloatArrayRenderbufferStorageFunction _renderbufferStorage;
    GLFloatArrayFramebufferRenderbufferFunction _framebufferRenderbuffer;
    GLFloatArrayCheckFramebufferStatusFunction _checkFramebufferStatus;
    GLFloatArrayClearBufferfvFunction _clearBufferfv;
    GLFloatArrayGetIntegervFunction _getIntegerv;
    GLFloatArrayGetErrorFunction _getError;
} GLFloatArrayApi;

/**
 * @brief One float array, the framebuffer that draws into its layers, and its optional depth. Zero it before
 *        GLFloatArray_Create; release it with GLFloatArray_Destroy. Read-only (underscore-prefixed) fields.
 */
typedef struct GLFloatArrayStruct
{
    /** @brief The texture's GL name (a GL_TEXTURE_2D_ARRAY of GL_R32F or GL_RG32F); 0 while not made. */
    unsigned int _texture;
    /** @brief The framebuffer's GL name (one layer as its colour at a time); 0 while not made. */
    unsigned int _framebuffer;
    /** @brief The depth renderbuffer's GL name (24 bits, the layers' size), 0 without one. */
    unsigned int _depth;
    /** @brief Each layer's side in texels, the layers and the channels a texel (1 or 2). */
    int _size;
    int _layerCount;
    int _channelCount;
} GLFloatArray;


// Functions.
/**
 * @brief Resolves the GL functions through rlGetProcAddress. Call once, with raylib's GL context current.
 * @param outApi [out] The API; _isAvailable false (and _missing which function) when one is missing.
 * @returns Whether the API is available.
 */
bool GLFloatArray_LoadApi(GLFloatArrayApi* outApi);

/**
 * @brief Makes the array and its framebuffer (with its first layer attached, and the depth renderbuffer when asked),
 *        checked complete. A layer holds nothing defined until it is cleared or drawn.
 *
 * Leaves unit 0's array target, the renderbuffer binding and the framebuffer binding as raylib expects them (nothing
 * bound). On any failure nothing is left behind and @p outArray stays zeroed.
 * @param api An available API; must not be NULL.
 * @param size Each layer's side in texels, 1 to GL_FLOAT_ARRAY_MAX_SIZE (and at most the GPU's largest texture).
 * @param layerCount The layers, 1 to GL_FLOAT_ARRAY_MAX_LAYERS (and at most the GPU's largest array).
 * @param channelCount 1 (GL_R32F) or 2 (GL_RG32F).
 * @param hasDepth Whether the framebuffer gets a depth renderbuffer (for draws that keep the nearest surface).
 * @param outArray [out] Receives the array; must not be NULL.
 * @returns Success; ErrorCode_InvalidState when the API is unavailable, GL refused the storage (the GPU has no room: its
 *          error in the message) or the framebuffer is incomplete; ErrorCode_ArgumentOutOfRange for a size, a count or a
 *          channel count outside the limits or beyond what the GPU allows.
 */
Error GLFloatArray_Create(const GLFloatArrayApi* api, int size, int layerCount, int channelCount, bool hasDepth,
    GLFloatArray* outArray);

/**
 * @brief Deletes the array's texture, framebuffer and depth (none bound afterwards), and zeroes it. Safe on a zeroed
 *        array.
 * @param api The API the array was made with; must not be NULL.
 * @param array The array; must not be NULL.
 */
void GLFloatArray_Destroy(const GLFloatArrayApi* api, GLFloatArray* array);

/**
 * @brief Makes one layer the framebuffer's colour (the draws that follow BeginTextureMode land there). Leaves the
 *        framebuffer unbound.
 * @param api The array's API; must not be NULL.
 * @param array A made array; must not be NULL.
 * @param layer The layer, below the array's layer count.
 */
void GLFloatArray_AttachLayer(const GLFloatArrayApi* api, const GLFloatArray* array, int layer);

/**
 * @brief The framebuffer to give BeginTextureMode (as a RenderTexture2D's id, its texture's size the array's).
 * @param array The array; must not be NULL.
 * @returns Its GL name (0 while not made).
 */
static inline unsigned int GLFloatArray_GetFramebuffer(const GLFloatArray* array)
{
    return array->_framebuffer;
}

/**
 * @brief Clears the bound framebuffer's colour to exact float values (raylib's queued batch is drawn out first), and its
 *        depth to the far plane when the array has a depth. Call between BeginTextureMode on the array's framebuffer and
 *        the draws.
 * @param api An available API; must not be NULL.
 * @param array The array whose framebuffer is bound; must not be NULL.
 * @param red The first channel's value.
 * @param green The second channel's (ignored by a one-channel array).
 */
void GLFloatArray_Clear(const GLFloatArrayApi* api, const GLFloatArray* array, float red, float green);

/**
 * @brief Binds the array on a texture unit's array target, for a sampler2DArray pointed at that unit. Leaves that unit
 *        active.
 * @param api The array's API; must not be NULL.
 * @param array A made array; must not be NULL.
 * @param unit The texture unit.
 */
void GLFloatArray_Bind(const GLFloatArrayApi* api, const GLFloatArray* array, int unit);
