#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "wr/WRError.h"

/*
 * Private header (source/): 2D texture arrays (GL_TEXTURE_2D_ARRAY), direct OpenGL calls (decision W7, B02; with
 * GLReadback.h's since T37 and GLDepth.h's since T39, the project's only ones). raylib has no texture arrays, and a
 * terrain blends up to sixteen materials per pixel anywhere, which raylib's twelve material map slots cannot hold; an
 * array holds one layer per material and the shader picks layers by index.
 *
 * WHY HERE. Everything else draws through raylib. These calls are kept in this one module, and their function pointers
 * come through raylib's rlGetProcAddress (the loader raylib itself was initialized with), so raylib is not rebuilt and no
 * GL header or loader library is added. The GL constants it needs are defined in GLTextureArray.c. BUILDING.md and
 * agents/AGENTS.md say so too; anything else that needs GL raylib lacks belongs here or in a module like it.
 *
 * USE. GLTextureArray_LoadApi once, after the GL context exists (the renderer's shader load): if a function is missing
 * the API is unavailable, the caller logs it once and draws without arrays. Then create arrays from CPU pixels (every
 * layer the same size, each mipmapped, filtered trilinearly, anisotropically where the driver has the extension, and
 * repeating), bind them to texture units for a draw, and delete them. Main thread (the GL thread) only. The binds touch
 * only the GL_TEXTURE_2D_ARRAY target of a unit, which raylib never uses, so raylib's own texture state is undisturbed.
 */


// Macros.
/* GL's calling convention: its functions are __stdcall on Windows (which matters for 32-bit builds; 64-bit has one
 * convention), the default elsewhere. The one platform difference in calling GL through pointers. */
#if defined(_WIN32)
#define GL_TEXTURE_ARRAY_APIENTRY __stdcall
#else
#define GL_TEXTURE_ARRAY_APIENTRY
#endif


// Types.
/**
 * @brief A texture array's texel format.
 */
typedef enum GLTextureArrayFormatEnum
{
    /** @brief Four 8-bit channels (red, green, blue, alpha), normalized: GL_RGBA8. */
    GLTextureArrayFormat_Rgba8 = 0,
    /** @brief Two 8-bit channels (red, green), normalized: GL_RG8. */
    GLTextureArrayFormat_Rg8 = 1
} GLTextureArrayFormat;

/* The GL functions' types, as GL declares them (int and unsigned int are GL's own sizes: GLsizei, GLint, GLenum,
 * GLuint). */
typedef void (GL_TEXTURE_ARRAY_APIENTRY* GLGenTexturesFunction)(int count, unsigned int* textures);
typedef void (GL_TEXTURE_ARRAY_APIENTRY* GLDeleteTexturesFunction)(int count, const unsigned int* textures);
typedef void (GL_TEXTURE_ARRAY_APIENTRY* GLBindTextureFunction)(unsigned int target, unsigned int texture);
typedef void (GL_TEXTURE_ARRAY_APIENTRY* GLTexImage3DFunction)(unsigned int target, int level, int internalFormat,
    int width, int height, int depth, int border, unsigned int format, unsigned int type, const void* pixels);
typedef void (GL_TEXTURE_ARRAY_APIENTRY* GLTexSubImage3DFunction)(unsigned int target, int level, int xOffset,
    int yOffset, int zOffset, int width, int height, int depth, unsigned int format, unsigned int type,
    const void* pixels);
typedef void (GL_TEXTURE_ARRAY_APIENTRY* GLGenerateMipmapFunction)(unsigned int target);
typedef void (GL_TEXTURE_ARRAY_APIENTRY* GLTexParameteriFunction)(unsigned int target, unsigned int name, int value);
typedef void (GL_TEXTURE_ARRAY_APIENTRY* GLTexParameterfFunction)(unsigned int target, unsigned int name, float value);
typedef void (GL_TEXTURE_ARRAY_APIENTRY* GLPixelStoreiFunction)(unsigned int name, int value);
typedef void (GL_TEXTURE_ARRAY_APIENTRY* GLGetIntegervFunction)(unsigned int name, int* values);
typedef void (GL_TEXTURE_ARRAY_APIENTRY* GLGetFloatvFunction)(unsigned int name, float* values);
typedef unsigned int (GL_TEXTURE_ARRAY_APIENTRY* GLGetErrorFunction)(void);

/**
 * @brief The GL functions texture arrays need, resolved through raylib's rlGetProcAddress. A plain value: fill it with
 *        GLTextureArray_LoadApi; nothing to release. Read-only after that.
 */
typedef struct GLTextureArrayApiStruct
{
    /** @brief Whether every function was found (the pointers are valid only then). */
    bool _isAvailable;
    /** @brief The anisotropic filtering the arrays get (1: none; the driver's largest, at most 8). */
    float _anisotropy;
    /** @brief The first function that was missing (a static string), for the caller's one log line; NULL when available. */
    const char* _missing;
    GLGenTexturesFunction _genTextures;
    GLDeleteTexturesFunction _deleteTextures;
    GLBindTextureFunction _bindTexture;
    GLTexImage3DFunction _texImage3D;
    GLTexSubImage3DFunction _texSubImage3D;
    GLGenerateMipmapFunction _generateMipmap;
    GLTexParameteriFunction _texParameteri;
    GLTexParameterfFunction _texParameterf;
    GLPixelStoreiFunction _pixelStorei;
    GLGetIntegervFunction _getIntegerv;
    GLGetFloatvFunction _getFloatv;
    GLGetErrorFunction _getError;
} GLTextureArrayApi;


// Functions.
/**
 * @brief Resolves the GL functions through rlGetProcAddress. Call once, with raylib's GL context current.
 * @param outApi [out] The API; _isAvailable false (and _missing the first function not found) when one is missing.
 * @returns Whether the API is available.
 */
bool GLTextureArray_LoadApi(GLTextureArrayApi* outApi);

/**
 * @brief Reports whether the API's functions were all found.
 * @param api The API; must not be NULL.
 * @returns true when texture arrays can be made.
 */
static inline bool GLTextureArray_IsAvailable(const GLTextureArrayApi* api)
{
    return api->_isAvailable;
}

/**
 * @brief Makes a texture array from CPU pixels: every layer @p width x @p height texels in @p format, mipmapped
 *        (glGenerateMipmap), filtered trilinearly (and anisotropically where available), repeating in both directions.
 *
 * A NULL layer is filled with @p fillTexel. Leaves no array bound and the unpack alignment as it was.
 * @param api An available API; must not be NULL.
 * @param format The texel format.
 * @param width The layers' width in texels (1 to the driver's limit; GL 3.3 guarantees 1024).
 * @param height The layers' height in texels.
 * @param layerCount How many layers (1 to 256, GL 3.3's guaranteed minimum).
 * @param layers @p layerCount pointers to each layer's texels (row by row, tightly packed: 4 or 2 bytes a texel), or NULL
 *        for a layer filled with @p fillTexel.
 * @param fillTexel One texel's bytes (4 or 2, as the format) for the NULL layers; may be NULL when no layer is.
 * @param outId [out] Receives the array's GL name (0 on failure). Must not be NULL.
 * @returns Success; ErrorCode_InvalidState when the API is unavailable, and when GL refused the storage (out of memory, or
 *          another GL error, its code in the message; nothing is left behind); ErrorCode_ArgumentOutOfRange for a size
 *          or layer count outside the limits; ErrorCode_IllegalArgument for a NULL layer without a fill texel.
 */
Error GLTextureArray_Create(const GLTextureArrayApi* api, GLTextureArrayFormat format, int32_t width, int32_t height,
    int32_t layerCount, const uint8_t* const* layers, const uint8_t* fillTexel, unsigned int* outId);

/**
 * @brief Binds an array to a texture unit's GL_TEXTURE_2D_ARRAY target (0 unbinds), then makes unit 0 active again.
 * @param api An available API; must not be NULL.
 * @param unit The texture unit (0 to 15).
 * @param id The array's GL name, or 0.
 */
void GLTextureArray_Bind(const GLTextureArrayApi* api, int32_t unit, unsigned int id);

/**
 * @brief Deletes an array (nothing for 0).
 * @param api An available API; must not be NULL.
 * @param id The array's GL name, or 0.
 */
void GLTextureArray_Delete(const GLTextureArrayApi* api, unsigned int id);
