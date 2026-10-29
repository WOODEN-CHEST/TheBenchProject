#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "wr/WRError.h"

/*
 * Private header (source/): slice maps, one of the modules of direct OpenGL calls (BUILDING.md, "Direct OpenGL calls";
 * GLTextureArray.h, GLDepth.h and GLReadback.h are the others). The sky's occupancy volume (interior lighting:
 * WorldInterior.h) is a 2D texture of unsigned integers, four 32-bit words a texel, whose bits are height slices; it is
 * drawn with a logic operation (XOR, then OR) instead of blending, cleared with integers, and reduced into a pyramid of
 * its own mipmap levels, each drawn from the one below. raylib has no integer textures, no logic operations and no
 * per-level reads.
 *
 * WHY HERE. Everything else draws through raylib. These calls are kept in this module, their function pointers resolved
 * through raylib's rlGetProcAddress (no raylib rebuild, no GL header or loader added), their constants defined in
 * GLSliceMap.c, and a missing function leaves the API unavailable (the caller logs it once and goes without).
 *
 * USE. GLSliceMap_LoadApi once, after the GL context exists. GLSliceMap_CreateTexture makes the texture with its
 * levels (attach each level to a framebuffer with rlFramebufferAttach and its mip level), GLSliceMap_DeleteTexture
 * frees it. Around a draw into a level: GLSliceMap_Clear for its words, GLSliceMap_BeginLogicOp and
 * GLSliceMap_EndLogicOp for its logic operation, GLSliceMap_ReadLevel to let a pass read the level below while it draws
 * the next (and GLSliceMap_ReadAllLevels after). Main thread (the GL thread) only.
 */


// Macros.
/* GL's calling convention through pointers: __stdcall on Windows (which matters for 32-bit builds), the default
 * elsewhere (as GLTextureArray.h's). */
#if defined(_WIN32)
#define GL_SLICE_MAP_APIENTRY __stdcall
#else
#define GL_SLICE_MAP_APIENTRY
#endif


// Types.
/**
 * @brief The logic operation a draw into a slice map combines its fragments' words with.
 */
typedef enum GLSliceMapOpEnum
{
    /** @brief Exclusive or: each fragment toggles its bits. */
    GLSliceMapOp_Xor = 0,
    /** @brief Or: each fragment sets its bits. */
    GLSliceMapOp_Or = 1
} GLSliceMapOp;

/* The GL functions' types, as GL declares them (int and unsigned int are GL's GLint and GLsizei, GLenum and GLuint). */
typedef void (GL_SLICE_MAP_APIENTRY* GLSliceMapGenTexturesFunction)(int count, unsigned int* textures);
typedef void (GL_SLICE_MAP_APIENTRY* GLSliceMapDeleteTexturesFunction)(int count, const unsigned int* textures);
typedef void (GL_SLICE_MAP_APIENTRY* GLSliceMapBindTextureFunction)(unsigned int target, unsigned int texture);
typedef void (GL_SLICE_MAP_APIENTRY* GLSliceMapTexImage2DFunction)(unsigned int target, int level, int internalFormat,
    int width, int height, int border, unsigned int format, unsigned int type, const void* pixels);
typedef void (GL_SLICE_MAP_APIENTRY* GLSliceMapTexParameteriFunction)(unsigned int target, unsigned int name, int value);
typedef void (GL_SLICE_MAP_APIENTRY* GLSliceMapEnableFunction)(unsigned int capability);
typedef void (GL_SLICE_MAP_APIENTRY* GLSliceMapLogicOpFunction)(unsigned int operation);
typedef void (GL_SLICE_MAP_APIENTRY* GLSliceMapClearBufferuivFunction)(unsigned int buffer, int drawBuffer,
    const unsigned int* value);
typedef unsigned int (GL_SLICE_MAP_APIENTRY* GLSliceMapGetErrorFunction)(void);

/**
 * @brief The GL functions slice maps need, resolved through raylib's rlGetProcAddress. A plain value: fill it with
 *        GLSliceMap_LoadApi; nothing to release. Read-only after that.
 */
typedef struct GLSliceMapApiStruct
{
    /** @brief Whether every function was found (the pointers are valid only then). */
    bool _isAvailable;
    /** @brief The first function that was missing (a static string), for the caller's one log line; NULL when
     *  available. */
    const char* _missing;
    GLSliceMapGenTexturesFunction _genTextures;
    GLSliceMapDeleteTexturesFunction _deleteTextures;
    GLSliceMapBindTextureFunction _bindTexture;
    GLSliceMapTexImage2DFunction _texImage2D;
    GLSliceMapTexParameteriFunction _texParameteri;
    GLSliceMapEnableFunction _enable;
    GLSliceMapEnableFunction _disable;
    GLSliceMapLogicOpFunction _logicOp;
    GLSliceMapClearBufferuivFunction _clearBufferuiv;
    GLSliceMapGetErrorFunction _getError;
} GLSliceMapApi;


// Functions.
/**
 * @brief Resolves the GL functions through rlGetProcAddress. Call once, with raylib's GL context current.
 * @param outApi [out] The API; _isAvailable false (and _missing why) when a function is missing.
 * @returns Whether the API is available.
 */
bool GLSliceMap_LoadApi(GLSliceMapApi* outApi);

/**
 * @brief Makes a slice map: a square 2D texture of four unsigned 32-bit words a texel (GL_RGBA32UI), with its mipmap
 *        levels down from a side of @p size (each half the last), read texel for texel (nearest, the nearest level:
 *        a fetch from any level is defined; clamped), every level readable. Leaves no texture bound on unit 0.
 * @param api An available API; must not be NULL.
 * @param size Level 0's side in texels (1 to 8192).
 * @param levelCount Its levels, 1 to the number a side of @p size halves to 1 in.
 * @param outId [out] Receives its GL name (0 on failure); must not be NULL.
 * @returns Success; ErrorCode_InvalidState when the API is unavailable or GL refused the storage (its error code in the
 *          message; nothing is left behind); ErrorCode_ArgumentOutOfRange for a size or a level count outside the limits.
 */
Error GLSliceMap_CreateTexture(const GLSliceMapApi* api, int size, int levelCount, unsigned int* outId);

/**
 * @brief Frees a slice map (nothing for 0).
 * @param api An available API; must not be NULL.
 * @param id Its GL name.
 */
void GLSliceMap_DeleteTexture(const GLSliceMapApi* api, unsigned int id);

/**
 * @brief Clears the bound framebuffer's first colour attachment, a slice map's level, to zero words (raylib's clear
 *        writes floats, which an integer attachment does not take). raylib's batch is drawn out first.
 * @param api An available API; must not be NULL.
 */
void GLSliceMap_Clear(const GLSliceMapApi* api);

/**
 * @brief Combines the following draws' fragments with the attachment's words by a logic operation (instead of
 *        writing them). raylib's batch is drawn out first.
 * @param api An available API; must not be NULL.
 * @param op The operation.
 */
void GLSliceMap_BeginLogicOp(const GLSliceMapApi* api, GLSliceMapOp op);

/**
 * @brief Ends the logic operation: draws write their fragments again (raylib's state). raylib's batch is drawn out
 *        first.
 * @param api An available API; must not be NULL.
 */
void GLSliceMap_EndLogicOp(const GLSliceMapApi* api);

/**
 * @brief Lets reads of a slice map see one level alone, as its level 0 (its base and highest level both @p level), so a
 *        pass can read the level below while it draws into the next. Leaves no texture bound on unit 0.
 * @param api An available API; must not be NULL.
 * @param id The slice map.
 * @param level The level read.
 */
void GLSliceMap_ReadLevel(const GLSliceMapApi* api, unsigned int id, int level);

/**
 * @brief Lets reads of a slice map see every level again (base 0, highest @p levelCount - 1). Leaves no texture bound on
 *        unit 0.
 * @param api An available API; must not be NULL.
 * @param id The slice map.
 * @param levelCount Its levels.
 */
void GLSliceMap_ReadAllLevels(const GLSliceMapApi* api, unsigned int id, int levelCount);
