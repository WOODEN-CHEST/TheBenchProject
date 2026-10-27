#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "wr/WRError.h"

/*
 * Private header (source/): reversed depth, one of the three modules of direct OpenGL calls (BUILDING.md,
 * "Direct OpenGL calls"; GLTextureArray.h and GLReadback.h are the others). The scene's camera passes draw with a depth
 * that runs from 1 at the near plane to 0 at infinity, in 32-bit float: a float's exponent spreads its precision
 * evenly over the distance's orders of magnitude, so a hill 20 km away is as well resolved, relative to its distance, as
 * a cup on a desk, and the projection needs no far plane. raylib's depth runs from 0 at the near plane to 1 at its fixed
 * far plane (4 km), in 24 bits: about 19 m steps at 4 km.
 *
 * WHAT IT NEEDS. glClipControl (OpenGL 4.5, or GL_ARB_clip_control: most desktop GPUs since 2014), so the projection's
 * depth lands in 0 to 1 unchanged (OpenGL otherwise maps -1 to 1 onto 0 to 1, which throws the precision away), a
 * depth test passing the nearer, greater depth (GL_GEQUAL), depth cleared to 0, and 32-bit float depth buffers
 * (GL_DEPTH_COMPONENT32F: raylib lets the driver pick, usually 24-bit integers). Without clip control the renderer keeps
 * raylib's projection and its 4 km.
 *
 * WHY HERE. Everything else draws through raylib. These calls are kept in this module, their function pointers resolved
 * through raylib's rlGetProcAddress (no raylib rebuild, no GL header or loader added), their constants defined in
 * GLDepth.c, and a missing function or version leaves the API unavailable (the caller logs it once and goes without).
 *
 * USE. GLDepth_LoadApi once, after the GL context exists. Make the depth of a target that draws reversed with
 * GLDepth_CreateTexture or GLDepth_CreateRenderbuffer (attach it with rlFramebufferAttach; raylib's UnloadRenderTexture
 * deletes it with the framebuffer). Around a reversed pass, from before its clear to after its last draw:
 * GLDepth_BeginReversed, then GLDepth_EndReversed, which gives raylib back its state (-1 to 1, GL_LEQUAL, cleared to 1):
 * the shadow passes, the 2D passes and the UI never see the reversed state. Main thread (the GL thread) only.
 */


// Macros.
/* GL's calling convention through pointers: __stdcall on Windows (which matters for 32-bit builds), the default
 * elsewhere (as GLTextureArray.h's). */
#if defined(_WIN32)
#define GL_DEPTH_APIENTRY __stdcall
#else
#define GL_DEPTH_APIENTRY
#endif


// Types.
/* The GL functions' types, as GL declares them (int and unsigned int are GL's GLint and GLsizei, GLenum and GLuint; a
 * double is GLdouble). */
typedef void (GL_DEPTH_APIENTRY* GLDepthClipControlFunction)(unsigned int origin, unsigned int depth);
typedef void (GL_DEPTH_APIENTRY* GLDepthDepthFuncFunction)(unsigned int function);
typedef void (GL_DEPTH_APIENTRY* GLDepthClearDepthFunction)(double depth);
typedef void (GL_DEPTH_APIENTRY* GLDepthGenTexturesFunction)(int count, unsigned int* textures);
typedef void (GL_DEPTH_APIENTRY* GLDepthDeleteTexturesFunction)(int count, const unsigned int* textures);
typedef void (GL_DEPTH_APIENTRY* GLDepthBindTextureFunction)(unsigned int target, unsigned int texture);
typedef void (GL_DEPTH_APIENTRY* GLDepthTexImage2DFunction)(unsigned int target, int level, int internalFormat, int width,
    int height, int border, unsigned int format, unsigned int type, const void* pixels);
typedef void (GL_DEPTH_APIENTRY* GLDepthTexParameteriFunction)(unsigned int target, unsigned int name, int value);
typedef void (GL_DEPTH_APIENTRY* GLDepthGenRenderbuffersFunction)(int count, unsigned int* renderbuffers);
typedef void (GL_DEPTH_APIENTRY* GLDepthDeleteRenderbuffersFunction)(int count, const unsigned int* renderbuffers);
typedef void (GL_DEPTH_APIENTRY* GLDepthBindRenderbufferFunction)(unsigned int target, unsigned int renderbuffer);
typedef void (GL_DEPTH_APIENTRY* GLDepthRenderbufferStorageFunction)(unsigned int target, unsigned int internalFormat,
    int width, int height);
typedef void (GL_DEPTH_APIENTRY* GLDepthGetIntegervFunction)(unsigned int name, int* values);
typedef const unsigned char* (GL_DEPTH_APIENTRY* GLDepthGetStringiFunction)(unsigned int name, unsigned int index);
typedef unsigned int (GL_DEPTH_APIENTRY* GLDepthGetErrorFunction)(void);

/**
 * @brief The GL functions reversed depth needs, resolved through raylib's rlGetProcAddress, and whether the GPU has clip
 *        control. A plain value: fill it with GLDepth_LoadApi; nothing to release. Read-only after that.
 */
typedef struct GLDepthApiStruct
{
    /** @brief Whether reversed depth can be drawn: every function found and clip control there (OpenGL 4.5 or
     *  GL_ARB_clip_control). The pointers are valid only then. */
    bool _isAvailable;
    /** @brief Why not, for the caller's one log line (a static string: the first function missing, or the GL version);
     *  NULL when available. */
    const char* _missing;
    GLDepthClipControlFunction _clipControl;
    GLDepthDepthFuncFunction _depthFunc;
    GLDepthClearDepthFunction _clearDepth;
    GLDepthGenTexturesFunction _genTextures;
    GLDepthDeleteTexturesFunction _deleteTextures;
    GLDepthBindTextureFunction _bindTexture;
    GLDepthTexImage2DFunction _texImage2D;
    GLDepthTexParameteriFunction _texParameteri;
    GLDepthGenRenderbuffersFunction _genRenderbuffers;
    GLDepthDeleteRenderbuffersFunction _deleteRenderbuffers;
    GLDepthBindRenderbufferFunction _bindRenderbuffer;
    GLDepthRenderbufferStorageFunction _renderbufferStorage;
    GLDepthGetIntegervFunction _getIntegerv;
    GLDepthGetStringiFunction _getStringi;
    GLDepthGetErrorFunction _getError;
} GLDepthApi;


// Functions.
/**
 * @brief Resolves the GL functions through rlGetProcAddress and checks for clip control. Call once, with raylib's GL
 *        context current.
 * @param outApi [out] The API; _isAvailable false (and _missing why) when reversed depth cannot be drawn.
 * @returns Whether the API is available.
 */
bool GLDepth_LoadApi(GLDepthApi* outApi);

/**
 * @brief Makes a samplable 32-bit float depth texture (GL_DEPTH_COMPONENT32F; nearest filtering, clamped), for a target
 *        whose depth later passes read. Leaves no texture bound on unit 0.
 * @param api An available API; must not be NULL.
 * @param width The texture's width in pixels (1 to 16384).
 * @param height Its height.
 * @param outId [out] Receives its GL name (0 on failure); must not be NULL.
 * @returns Success; ErrorCode_InvalidState when the API is unavailable or GL refused the storage (its error code in the
 *          message; nothing is left behind); ErrorCode_ArgumentOutOfRange for a size outside the limits.
 */
Error GLDepth_CreateTexture(const GLDepthApi* api, int width, int height, unsigned int* outId);

/**
 * @brief Makes a 32-bit float depth renderbuffer, for a target whose depth no pass reads. Leaves no renderbuffer bound.
 * @param api An available API; must not be NULL.
 * @param width Its width in pixels (1 to 16384).
 * @param height Its height.
 * @param outId [out] Receives its GL name (0 on failure); must not be NULL.
 * @returns As GLDepth_CreateTexture's.
 */
Error GLDepth_CreateRenderbuffer(const GLDepthApi* api, int width, int height, unsigned int* outId);

/**
 * @brief Switches GL to reversed depth: clip control's 0-to-1 depth, the depth test passing the greater depth
 *        (GL_GEQUAL), depth cleared to 0. Call before a reversed pass's clear (raylib's batch is drawn out first, in the
 *        state it was queued in).
 * @param api An available API; must not be NULL.
 */
void GLDepth_BeginReversed(const GLDepthApi* api);

/**
 * @brief Gives raylib back its depth state: clip control's -1-to-1 depth, GL_LEQUAL, depth cleared to 1 (rlgl's own
 *        settings). Call after a reversed pass's last draw (its batch is drawn out first).
 * @param api An available API; must not be NULL.
 */
void GLDepth_EndReversed(const GLDepthApi* api);
