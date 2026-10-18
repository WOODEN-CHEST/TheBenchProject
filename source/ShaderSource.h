#pragma once
#include <stddef.h>
#include "AssetManager.h"


/*
 * Private header (source/): expands the #include lines of a shader stage's source before it is compiled (the shader
 * loader uses it; tests use it too).
 *
 * SYNTAX. A line `#include "common/atmosphere"`, with optional spaces or tabs before the '#', between the '#' and
 * "include", and before the quoted location; a trailing `//` comment is allowed. The location is an asset file location
 * like every other one: relative to the shader type's directory, without extension, resolved with
 * AssetManager_OpenResource and the shader-stage extensions, so "common/atmosphere" finds
 * shaders/common/atmosphere.glsl. Shared files have no JSON definition next to them: they are resources, not shader
 * assets.
 *
 * RULES.
 *  - Includes nest at most SHADER_SOURCE_MAX_INCLUDE_DEPTH levels deep (the main file is level 0).
 *  - A file included a second time in the same stage is skipped, as if every file had include guards. The main file
 *    counts as included, and one leading '/' of a location is ignored (as the resolver ignores it).
 *  - An included file must not contain a #version line, and no #include may come before the main file's #version
 *    line, so the main file's #version stays the first line of the expanded text.
 *  - Every other line passes through untouched, unknown directives included. The expander works line by line and
 *    before the GLSL preprocessor: it does not see comments or #if blocks, so an #include inside a block comment or a
 *    disabled #if block is still expanded (and counts as included). Keep includes out of conditional blocks.
 *  - A NUL byte in any file is an error (it would end the source string silently).
 *
 * LINE NUMBERS. Every included file is a GLSL source string of its own: the main file is number 0 and each included
 * file gets the next number, in the order they are first included. Before an included file's text the expander writes
 * `#line 1 <n>`, after it `#line <line after the #include> <number of the including file>`, and a repeated include
 * becomes an empty line, so a compiler error such as `2(14)` (NVIDIA) or `2:14(5)` (Mesa) names line 14 of file 2.
 * This uses the #line rules of GLSL 3.30 and later (the next line gets the number written); every shader of the game
 * declares #version 330.
 */


// Macros.
/** @brief The deepest include nesting the expander accepts: the main file is level 0, its includes level 1. */
#define SHADER_SOURCE_MAX_INCLUDE_DEPTH ((size_t)8)


// Functions.
/**
 * @brief Expands every #include line of a shader stage's source text (see the file comment for the syntax and rules).
 *
 * Included files are read through @p assets, so they follow the search roots and resource-pack overrides like every
 * other resource. Scratch buffers are borrowed from the manager's pools and returned before this returns.
 * @param assets The manager that resolves include locations; must not be NULL.
 * @param shaderType The shader asset type: include locations resolve under its directory.
 * @param mainLocation Where @p text came from (the stage's own location). Its value names source string 0 in messages
 *        and in @p outSourceList, and a File location also counts as already included. Must not be NULL, and its Value
 *        must not be NULL.
 * @param text The stage's source text; it need not be NUL-terminated. May be NULL only when @p length is 0.
 * @param length Number of bytes in @p text.
 * @param outText [out] Byte buffer the expanded text is appended to, NUL-terminated. It is treated as one growing
 *        string: an existing trailing NUL is dropped first. On failure it is restored to its previous contents. Must
 *        not be NULL.
 * @param outSourceList [out] Byte buffer that receives, when the stage included at least one file, the source-string
 *        numbers for messages, e.g. `0 = "sky/fragment", 1 = "common/math", 2 = "common/atmosphere"`, appended as one
 *        growing NUL-terminated string. Nothing is appended when nothing was included or on failure. May be NULL.
 * @returns Success; ErrorCode_IllegalArgument for a NULL argument; ErrorCode_InvalidAssetData for a malformed
 *          #include, an #include before the main file's #version, an include nested too deep, an included file that
 *          cannot be found or read (the message names the include, the including file and the line), a #version line
 *          in an included file, or a NUL byte; ErrorCode_BufferTooLarge when a buffer cannot grow.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error ShaderSource_ExpandIncludes(AssetManager* assets, AssetTypeID shaderType, const AssetLocation* mainLocation,
    const unsigned char* text, size_t length, GenericBuffer* outText, GenericBuffer* outSourceList);
