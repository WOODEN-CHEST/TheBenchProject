#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "wr/WRError.h"

/*
 * Private header (source/): the sun's and the moon's shadow maps as one texture, one of the modules of direct OpenGL
 * calls (BUILDING.md, "Direct OpenGL calls"; GLTextureArray.h, GLReadback.h, GLDepth.h and GLSliceMap.h are the others).
 *
 * WHAT IT IS. A texture array of 32-bit float depth (GL_TEXTURE_2D_ARRAY, GL_DEPTH_COMPONENT32F), one layer per shadow
 * level (WorldShadowLevels.h), read with the hardware's depth comparison (GL_COMPARE_REF_TO_TEXTURE, GL_LEQUAL) and
 * linear filtering: one lookup through a sampler2DArrayShadow compares the four texels around a point and weighs them
 * bilinearly, a soft one-texel edge for the price of one fetch. Every level sits behind one texture unit, so the levels
 * cost the lit programs one unit whatever their number. A framebuffer without colour takes one layer at a time as its
 * depth, and the levels' casters draw with depth clamping on: what lies nearer the light than a level's box (a tower
 * toward a low sun) lands on its near plane instead of being cut off.
 *
 * WHY HERE. raylib has no texture arrays, no depth comparison, no layer attachments and no depth clamping. These calls
 * are kept in this module, their function pointers resolved through raylib's rlGetProcAddress (no raylib rebuild, no GL
 * header or loader added), their constants defined in GLShadowArray.c; every one is OpenGL 3.2 core at the newest, which
 * raylib's 3.3 already requires. A missing function, or a GL error after making the array (a loader may hand out a stub
 * for a function the context lacks), leaves the array unmade: the caller logs it once and draws without sun and moon
 * shadows.
 *
 * USE. GLShadowArray_LoadApi once, after the GL context exists; GLShadowArray_Create for the maps, GLShadowArray_Destroy
 * to release them. To draw a level: GLShadowArray_AttachLayer, then raylib's BeginTextureMode with a RenderTexture2D
 * whose id is GLShadowArray_GetFramebuffer and whose texture's size is the array's (it binds the framebuffer and sets the
 * viewport), clear, draw between GLShadowArray_BeginDepthClamp and GLShadowArray_EndDepthClamp, EndTextureMode. To read
 * the levels: GLShadowArray_Bind on the lit programs' unit. Main thread (the GL thread) only.
 */


// Macros.
/* GL's calling convention through pointers: __stdcall on Windows (which matters for 32-bit builds), the default
 * elsewhere (as GLTextureArray.h's). */
#if defined(_WIN32)
#define GL_SHADOW_ARRAY_APIENTRY __stdcall
#else
#define GL_SHADOW_ARRAY_APIENTRY
#endif

/** @brief The largest side a map may have here, in texels. */
#define GL_SHADOW_ARRAY_MAX_SIZE 8192
/** @brief The most layers an array may have here. */
#define GL_SHADOW_ARRAY_MAX_LAYERS 16


// Types.
/* The GL functions' types, as GL declares them (int and unsigned int are GL's GLint and GLsizei, GLenum and GLuint). */
typedef void (GL_SHADOW_ARRAY_APIENTRY* GLShadowArrayGenFunction)(int count, unsigned int* names);
typedef void (GL_SHADOW_ARRAY_APIENTRY* GLShadowArrayDeleteFunction)(int count, const unsigned int* names);
typedef void (GL_SHADOW_ARRAY_APIENTRY* GLShadowArrayBindFunction)(unsigned int target, unsigned int name);
typedef void (GL_SHADOW_ARRAY_APIENTRY* GLShadowArrayTexImage3DFunction)(unsigned int target, int level,
    int internalFormat, int width, int height, int depth, int border, unsigned int format, unsigned int type,
    const void* pixels);
typedef void (GL_SHADOW_ARRAY_APIENTRY* GLShadowArrayTexParameteriFunction)(unsigned int target, unsigned int name,
    int value);
typedef void (GL_SHADOW_ARRAY_APIENTRY* GLShadowArrayFramebufferTextureLayerFunction)(unsigned int target,
    unsigned int attachment, unsigned int texture, int level, int layer);
typedef void (GL_SHADOW_ARRAY_APIENTRY* GLShadowArrayBufferFunction)(unsigned int buffer);
typedef unsigned int (GL_SHADOW_ARRAY_APIENTRY* GLShadowArrayCheckFramebufferStatusFunction)(unsigned int target);
typedef void (GL_SHADOW_ARRAY_APIENTRY* GLShadowArrayCapabilityFunction)(unsigned int capability);
typedef void (GL_SHADOW_ARRAY_APIENTRY* GLShadowArrayGetIntegervFunction)(unsigned int name, int* values);
typedef unsigned int (GL_SHADOW_ARRAY_APIENTRY* GLShadowArrayGetErrorFunction)(void);

/**
 * @brief The GL functions the shadow array needs, resolved through raylib's rlGetProcAddress. A plain value: fill it with
 *        GLShadowArray_LoadApi; nothing to release. Read-only after that.
 */
typedef struct GLShadowArrayApiStruct
{
    /** @brief Whether every function was found (the pointers are valid only then). */
    bool _isAvailable;
    /** @brief The first function missing, for the caller's one log line (a static string); NULL when available. */
    const char* _missing;
    GLShadowArrayGenFunction _genTextures;
    GLShadowArrayDeleteFunction _deleteTextures;
    GLShadowArrayBindFunction _bindTexture;
    GLShadowArrayTexImage3DFunction _texImage3D;
    GLShadowArrayTexParameteriFunction _texParameteri;
    GLShadowArrayGenFunction _genFramebuffers;
    GLShadowArrayDeleteFunction _deleteFramebuffers;
    GLShadowArrayBindFunction _bindFramebuffer;
    GLShadowArrayFramebufferTextureLayerFunction _framebufferTextureLayer;
    GLShadowArrayBufferFunction _drawBuffer;
    GLShadowArrayBufferFunction _readBuffer;
    GLShadowArrayCheckFramebufferStatusFunction _checkFramebufferStatus;
    GLShadowArrayCapabilityFunction _enable;
    GLShadowArrayCapabilityFunction _disable;
    GLShadowArrayGetIntegervFunction _getIntegerv;
    GLShadowArrayGetErrorFunction _getError;
} GLShadowArrayApi;

/**
 * @brief One array of shadow maps and the framebuffer that draws into its layers. Zero it before GLShadowArray_Create;
 *        release it with GLShadowArray_Destroy. Read-only (underscore-prefixed) fields.
 */
typedef struct GLShadowArrayStruct
{
    /** @brief The texture's GL name (a GL_TEXTURE_2D_ARRAY of GL_DEPTH_COMPONENT32F); 0 while not made. */
    unsigned int _texture;
    /** @brief The framebuffer's GL name (no colour; its depth one layer at a time); 0 while not made. */
    unsigned int _framebuffer;
    /** @brief Each layer's side, in texels. */
    int _size;
    /** @brief The number of layers. */
    int _layerCount;
} GLShadowArray;


// Functions.
/**
 * @brief Resolves the GL functions through rlGetProcAddress. Call once, with raylib's GL context current.
 * @param outApi [out] The API; _isAvailable false (and _missing which function) when one is missing.
 * @returns Whether the API is available.
 */
bool GLShadowArray_LoadApi(GLShadowArrayApi* outApi);

/**
 * @brief Makes the array and its framebuffer, checked complete. A layer holds nothing defined until a level is drawn
 *        into it (every level renders before it is first read).
 *
 * Leaves unit 0's array target and the framebuffer binding as raylib expects them (nothing bound). On any failure
 * nothing is left behind and @p outArray stays zeroed.
 * @param api An available API; must not be NULL.
 * @param size Each layer's side in texels, 1 to GL_SHADOW_ARRAY_MAX_SIZE (and at most the GPU's largest texture).
 * @param layerCount The layers, 1 to GL_SHADOW_ARRAY_MAX_LAYERS (and at most the GPU's largest array).
 * @param outArray [out] Receives the array; must not be NULL.
 * @returns Success; ErrorCode_InvalidState when the API is unavailable, GL refused the storage (the GPU has no room: its
 *          error in the message) or the framebuffer is incomplete; ErrorCode_ArgumentOutOfRange for a size or count
 *          outside the limits or beyond what the GPU allows.
 */
Error GLShadowArray_Create(const GLShadowArrayApi* api, int size, int layerCount, GLShadowArray* outArray);

/**
 * @brief Deletes the array's texture and framebuffer (none bound afterwards), and zeroes it. Safe on a zeroed array.
 * @param api The API the array was made with; must not be NULL.
 * @param array The array; must not be NULL.
 */
void GLShadowArray_Destroy(const GLShadowArrayApi* api, GLShadowArray* array);

/**
 * @brief Makes one layer the framebuffer's depth (the draws that follow BeginTextureMode land there). Leaves the
 *        framebuffer unbound.
 * @param api The array's API; must not be NULL.
 * @param array A made array; must not be NULL.
 * @param layer The layer, below the array's layer count.
 */
void GLShadowArray_AttachLayer(const GLShadowArrayApi* api, const GLShadowArray* array, int layer);

/**
 * @brief The framebuffer to give BeginTextureMode (as a RenderTexture2D's id, its texture's size the array's).
 * @param array The array; must not be NULL.
 * @returns Its GL name (0 while not made).
 */
static inline unsigned int GLShadowArray_GetFramebuffer(const GLShadowArray* array)
{
    return array->_framebuffer;
}

/**
 * @brief Switches depth clamping on for the draws that follow (raylib's queued batch is drawn out first): depth beyond
 *        the near or far plane is clamped onto it instead of the primitive being clipped there.
 * @param api An available API; must not be NULL.
 */
void GLShadowArray_BeginDepthClamp(const GLShadowArrayApi* api);

/**
 * @brief Switches depth clamping off again (raylib's queued batch is drawn out first), as raylib keeps it.
 * @param api An available API; must not be NULL.
 */
void GLShadowArray_EndDepthClamp(const GLShadowArrayApi* api);

/**
 * @brief Binds the array on a texture unit's array target, for a sampler2DArrayShadow pointed at that unit. Leaves that
 *        unit active.
 * @param api The array's API; must not be NULL.
 * @param array A made array; must not be NULL.
 * @param unit The texture unit.
 */
void GLShadowArray_Bind(const GLShadowArrayApi* api, const GLShadowArray* array, int unit);
