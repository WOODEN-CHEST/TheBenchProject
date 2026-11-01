#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "wr/WRError.h"

/*
 * Private header (source/): 3D textures (GL_TEXTURE_3D), direct OpenGL calls (with GLTextureArray.h's, GLReadback.h's,
 * GLDepth.h's, GLSliceMap.h's and GLShadowArray.h's, the project's only ones). raylib has no 3D textures, and the
 * clouds' noise is a volume the cloud march reads at any point with the hardware's trilinear filtering, wrapping on every
 * axis (a 2D atlas of slices would take two lookups and a mix per read, and seams between its slices).
 *
 * WHY HERE. Everything else draws through raylib. These calls are kept in this one module, and their function pointers
 * come through raylib's rlGetProcAddress (the loader raylib itself was initialized with), so raylib is not rebuilt and no
 * GL header or loader library is added. The GL constants it needs are defined in GLTexture3D.c. BUILDING.md and
 * agents/AGENTS.md say so too.
 *
 * USE. GLTexture3D_LoadApi once, after the GL context exists (the renderer's shader load): if a function is missing the
 * API is unavailable, the caller logs it once and goes without the feature. Then create textures from CPU texels (one
 * channel or four, one byte each, mipmapped, filtered trilinearly, repeating on every axis), bind them to texture units
 * for a draw, and delete them. Main thread (the GL thread) only. The binds touch only the GL_TEXTURE_3D target of a unit,
 * which raylib never uses, so raylib's own texture state is undisturbed.
 */


// Macros.
/* GL's calling convention: its functions are __stdcall on Windows (which matters for 32-bit builds; 64-bit has one
 * convention), the default elsewhere. */
#if defined(_WIN32)
#define GL_TEXTURE_3D_APIENTRY __stdcall
#else
#define GL_TEXTURE_3D_APIENTRY
#endif


// Types.
/**
 * @brief A 3D texture's texel format.
 */
typedef enum GLTexture3DFormatEnum
{
    /** @brief One 8-bit channel (red), normalized: GL_R8. */
    GLTexture3DFormat_R8 = 0,
    /** @brief Four 8-bit channels (red, green, blue, alpha), normalized: GL_RGBA8. */
    GLTexture3DFormat_Rgba8 = 1
} GLTexture3DFormat;

/* The GL functions' types, as GL declares them (int and unsigned int are GL's own sizes: GLsizei, GLint, GLenum,
 * GLuint). */
typedef void (GL_TEXTURE_3D_APIENTRY* GLTexture3DGenTexturesFunction)(int count, unsigned int* textures);
typedef void (GL_TEXTURE_3D_APIENTRY* GLTexture3DDeleteTexturesFunction)(int count, const unsigned int* textures);
typedef void (GL_TEXTURE_3D_APIENTRY* GLTexture3DBindTextureFunction)(unsigned int target, unsigned int texture);
typedef void (GL_TEXTURE_3D_APIENTRY* GLTexture3DTexImage3DFunction)(unsigned int target, int level, int internalFormat,
    int width, int height, int depth, int border, unsigned int format, unsigned int type, const void* pixels);
typedef void (GL_TEXTURE_3D_APIENTRY* GLTexture3DGenerateMipmapFunction)(unsigned int target);
typedef void (GL_TEXTURE_3D_APIENTRY* GLTexture3DTexParameteriFunction)(unsigned int target, unsigned int name,
    int value);
typedef void (GL_TEXTURE_3D_APIENTRY* GLTexture3DPixelStoreiFunction)(unsigned int name, int value);
typedef void (GL_TEXTURE_3D_APIENTRY* GLTexture3DGetIntegervFunction)(unsigned int name, int* values);
typedef unsigned int (GL_TEXTURE_3D_APIENTRY* GLTexture3DGetErrorFunction)(void);

/**
 * @brief The GL functions 3D textures need, resolved through raylib's rlGetProcAddress. A plain value: fill it with
 *        GLTexture3D_LoadApi; nothing to release. Read-only after that.
 */
typedef struct GLTexture3DApiStruct
{
    /** @brief Whether every function was found (the pointers are valid only then). */
    bool _isAvailable;
    /** @brief The first function that was missing (a static string), for the caller's one log line; NULL when available. */
    const char* _missing;
    GLTexture3DGenTexturesFunction _genTextures;
    GLTexture3DDeleteTexturesFunction _deleteTextures;
    GLTexture3DBindTextureFunction _bindTexture;
    GLTexture3DTexImage3DFunction _texImage3D;
    GLTexture3DGenerateMipmapFunction _generateMipmap;
    GLTexture3DTexParameteriFunction _texParameteri;
    GLTexture3DPixelStoreiFunction _pixelStorei;
    GLTexture3DGetIntegervFunction _getIntegerv;
    GLTexture3DGetErrorFunction _getError;
} GLTexture3DApi;


// Functions.
/**
 * @brief Resolves the GL functions through rlGetProcAddress. Call once, with raylib's GL context current.
 * @param outApi [out] The API; _isAvailable false (and _missing the first function not found) when one is missing.
 * @returns Whether the API is available.
 */
bool GLTexture3D_LoadApi(GLTexture3DApi* outApi);

/**
 * @brief Reports whether the API's functions were all found.
 * @param api The API; must not be NULL.
 * @returns true when 3D textures can be made.
 */
static inline bool GLTexture3D_IsAvailable(const GLTexture3DApi* api)
{
    return api->_isAvailable;
}

/**
 * @brief Makes a 3D texture from CPU texels: @p width x @p height x @p depth texels in @p format, mipmapped
 *        (glGenerateMipmap), filtered trilinearly, repeating on all three axes.
 *
 * Leaves no 3D texture bound and the unpack alignment as it was.
 * @param api An available API; must not be NULL.
 * @param format The texel format.
 * @param width The texture's width in texels (1 to 2048: GL 3.3 guarantees 256, larger may be refused).
 * @param height Its height in texels.
 * @param depth Its depth in texels.
 * @param texels The texels: x fastest, then y, then z, tightly packed (1 or 4 bytes a texel, as the format). Must not be
 *        NULL.
 * @param outId [out] Receives the texture's GL name (0 on failure). Must not be NULL.
 * @returns Success; ErrorCode_InvalidState when the API is unavailable, and when GL refused the storage (out of memory, or
 *          another GL error, its code in the message; nothing is left behind); ErrorCode_ArgumentOutOfRange for a size
 *          outside the limits.
 */
Error GLTexture3D_Create(const GLTexture3DApi* api, GLTexture3DFormat format, int32_t width, int32_t height,
    int32_t depth, const uint8_t* texels, unsigned int* outId);

/**
 * @brief Binds a 3D texture to a texture unit's GL_TEXTURE_3D target (0 unbinds), then makes unit 0 active again.
 * @param api An available API; must not be NULL.
 * @param unit The texture unit (0 to 15).
 * @param id The texture's GL name, or 0.
 */
void GLTexture3D_Bind(const GLTexture3DApi* api, int32_t unit, unsigned int id);

/**
 * @brief Deletes a 3D texture (nothing for 0).
 * @param api An available API; must not be NULL.
 * @param id The texture's GL name, or 0.
 */
void GLTexture3D_Delete(const GLTexture3DApi* api, unsigned int id);
