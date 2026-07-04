#pragma once
#include "raylib/raylib.h"
#include <stddef.h>
#include "wr/WRCompile.h"


/**
 * The renderer stuff for ErrDLogi.
 *
 * The render context is used for all render functions.
 * It can have an optional custom render buffer which is just a target to render to. If it isn't present
 * (_hasCustomRenderBuffer is false), then the rendering is done to the window's backbuffer and the render buffer properties
 * are those of the window backbuffer.
 * The render context is supposed to be constructed before rendering. It is safe and recommended to just create the render context objects
 * via the create method before rendering a frame, they can be disposed after the frame. Though probably shouldn't heap allocate them
 * every frame, use stack variables instead, as the render context objects do not require any data to be stored on the heap.
 *
 *
 * To help make rendering more consistent across screen sizes, the render context stores some aspect ratio data.
 * The target aspect ratio is the aspect ratio that the game is supposed to be viewed in. An imaginary area called the
 * render target area, which is the target aspect ratio, is placed in the render buffer. It is fitted perfectly so that one if its
 * lengths is the render buffer's length. For example, a target ratio of 16:9 on a 1920*1200 render buffer, the render target area would
 * be 1920*1080, the remaining 120 pixels would be outside of it.
 *     The render target area relative position just says where to
 * place the target area if the render buffer's aspect ratio isn't the target one. If set to (0.5, 0.5), then the target area will
 * always be placed in the middle of the render buffer with even spacing on both sides (horizontal or vertical) if the render buffer's
 * aspect ratio is not the target aspect ratio. Following the previous example, that would be 60 pixels from the top,
 * with 60 remaining to the bottom.
 *
 * The render position for 2D textures and text is specified by a render position struct. It has a vector value and render type.
 * If set to pixel type, the vector represents render buffer pixel coordinates.
 * If set to normalized relative, it represents a normalized [0;1] coordinate position in the render buffer.
 * If set to normalized fitted, the normalized [0;1] coordinates correspond to the target render area, not whole render buffer.
 * In all cases, the coordinates may be out of their range and still be fine (so not hard-clamped to their range).
 *
 * The render size for 2D textures works the same as the position. Either pixel size, window size or render target area size.
 *
 * For fonts, a normalized size of 1 means that vertically a single line's height will take up 100%
 * of the render value type height. That means, for normalized fitted coords: 100% of the render target area height.
 * For normalized relative: 100% of the render buffer height. Pixel value type still means 1 pixel for such a character,
 */


/** @brief Minimum accepted render color opacity (fully transparent). */
#define RENDER_COLOR_OPACITY_MIN 0.0f
/** @brief Maximum accepted render color opacity (fully opaque). */
#define RENDER_COLOR_OPACITY_MAX 1.0f
/** @brief Minimum accepted render color brightness (fully dark). */
#define RENDER_COLOR_BRIGHTNESS_MIN 0.0f
/** @brief Maximum accepted render color brightness (unmodified). */
#define RENDER_COLOR_BRIGHTNESS_MAX 1.0f


/**
 * @brief Per-frame rendering state: the target buffer plus the aspect-ratio fitting configuration.
 *
 * Describes where drawing goes (a custom render buffer or, when none is set, the window backbuffer) and
 * how normalized coordinates map onto it, and accumulates simple draw counters. Owns nothing: any custom
 * render buffer is borrowed and is not freed by RenderContext_Deconstruct. Intended to be created cheaply
 * (typically on the stack) each frame via RenderContext_Create; see the module comment for the coordinate
 * spaces it defines.
 */
typedef struct RenderContextStruct
{
    /** @brief true if drawing targets @c _renderBuffer; false if it targets the window backbuffer. */
    bool _hasCustomRenderBuffer;
    /** @brief The custom render target; borrowed and only valid when @c _hasCustomRenderBuffer is true. */
    RenderTexture2D _renderBuffer;
    /** @brief Size of the active render buffer, in pixels. */
    Vector2 _renderBufferSizePixels;
    /** @brief Aspect ratio (width / height) of the active render buffer. */
    float _renderBufferAspectRatio;

    /** @brief The aspect ratio the game is designed to be viewed in; defines the render target area. */
    float _targetAspectRatio;
    /** @brief Normalized [0;1] placement of the target area within the render buffer when aspect ratios differ; (0.5, 0.5) centers it. */
    Vector2 _targetRelativePosition;

    /** @brief Number of textures drawn through this context since it was created. */
    size_t _textureDrawCount;
    /** @brief Number of text strings drawn through this context since it was created. */
    size_t _stringDrawCount;
    /** @brief Number of models drawn through this context since it was created. */
    size_t _modelDrawCount;
    /** @brief Number of meshes drawn through this context since it was created; instanced draws count each instance. */
    size_t _meshDrawCount;
} RenderContext;

/**
 * @brief A drawing color expressed as a tint with separate brightness and opacity controls.
 *
 * The final raylib color is produced by RenderColor_GetFinalColor: the tint's RGB is scaled by
 * @c Brightness and its alpha is replaced by @c Opacity (the tint's own alpha is ignored).
 */
typedef struct RenderColorStruct
{
    /** @brief Base tint color; its alpha channel is ignored (opacity comes from @c Opacity). */
    Color Tint; // Alpha value ignored.
    /** @brief Brightness multiplier applied to the tint's RGB, in [0;1]; clamped when resolved. */
    float Brightness; // [0;1]
    /** @brief Final alpha as a fraction, in [0;1]; clamped when resolved. */
    float Opacity; // [0;1]
} RenderColor;

/**
 * @brief The coordinate space a RenderFloat or RenderVector2D value is expressed in.
 *
 * Selects how the renderer interprets a position or size before drawing; see the module comment for the
 * full description of each space.
 */
typedef enum RenderValueTypeEnum
{
    /** @brief A render-buffer pixel coordinate. */
    RenderValueType_Pixel,

    /** @brief Values in range [0;1], (0, 0) being top left and (1, 1) bottom right of the render buffer. */
    RenderValueType_NormalizedRelative,

    /** @brief Values in range [0;1], (0, 0) being top left of the target render area and (1, 1) its bottom right. */
    RenderValueType_NormalizedFitted
} RenderValueType;

/**
 * @brief A scalar value tagged with the coordinate space it is measured in.
 *
 * Used for one-dimensional quantities such as font sizes; the renderer converts @c Value according to
 * @c Type. Construct with the RenderFloat_Window / RenderFloat_Relative / RenderFloat_Fitted helpers.
 */
typedef struct RenderFloatStruct
{
    /** @brief The scalar magnitude, interpreted per @c Type. */
    float Value;
    /** @brief The coordinate space @c Value is expressed in. */
    RenderValueType Type;
} RenderFloat;

/**
 * @brief A 2D value tagged with the coordinate space it is measured in.
 *
 * Used for positions and sizes; the renderer converts @c Value according to @c Type. Construct with the
 * RenderVector2D_Window / RenderVector2D_Relative / RenderVector2D_Fitted helpers.
 */
typedef struct RenderVector2DStruct
{
    /** @brief The 2D magnitude/position, interpreted per @c Type. */
    Vector2 Value;
    /** @brief The coordinate space @c Value is expressed in. */
    RenderValueType Type;
} RenderVector2D;

/**
 * @brief The full set of parameters for drawing a texture through a render context.
 */
typedef struct TextureRenderArgumentsStruct
{
    /** @brief The texture to draw; borrowed for the duration of the draw call. */
    Texture2D Texture;
    /** @brief Where to place the texture; the origin (see @c RelativeOrigin) is positioned here. */
    RenderVector2D Position; // Position, the texture will be rendered as if the origin is placed here.
    /** @brief Normalized [0;1] sub-rectangle of the texture to draw (source region). */
    Rectangle RelativeSourceRectangle; // Normalized [0;1] area of the texture to draw.
    /** @brief Size of the drawn rectangle in the target space. */
    RenderVector2D Size; // Total size of the final rectangle that will be drawn.
    /** @brief Origin within the drawn rectangle, normalized [0;1]; also the rotation pivot. */
    Vector2 RelativeOrigin; // Relative origin in the texture.
    /** @brief Rotation around the origin, in radians. */
    float RotationRad; // Rotation around origin, in radians.
    /** @brief The color/tint to draw with. */
    RenderColor TargetColor; // The rendering color.
} TextureRenderArguments;

/**
 * @brief The full set of parameters for drawing a text string through a render context.
 *
 * Optionally carries a cached measurement of the text to avoid recomputing it every frame; see
 * @c CachedDrawSize and @c HasCachedDrawSize.
 */
typedef struct TextRenderArgumentsStruct
{
    /** @brief The text to draw as null-terminated UTF-8; borrowed for the duration of the draw call. */
    const unsigned char* Text; // UTF-8 Unicode text, null terminated.
    /** @brief The font to render the text with. */
    Font TargetFont;
    /** @brief Where to place the text; the origin (see @c RelativeOrigin) is positioned here. */
    RenderVector2D Position; // Position, the text will be rendered as if the origin is placed here.
    /** @brief Font size to render at, in the tagged coordinate space. */
    RenderFloat Size; // Size of the font to use.
    /** @brief Spacing between characters as a multiplier of the size value. */
    float SizeRelativeSpacing; // Spacing between characters for the text, a multiplier of the size value basically.
    /** @brief Origin within the whole text block, normalized [0;1]; also the rotation pivot. */
    Vector2 RelativeOrigin; // Relative origin in the entire text.
    /** @brief Rotation of the text around the origin, in radians. */
    float RotationRad; // Rotation of the text around the origin, in radians.
    /** @brief The text color. */
    RenderColor TargetColor; // The text color.

    /** @brief Cached normalized draw size; valid only when @c HasCachedDrawSize is true.
    * For performance reasons it is recommended to pass in the draw size of the text to the draw functions.
    * If the draw size is not cached, the renderer recalculates it automatically.
    * CachedDrawSize must be the normalized size returned by Renderer_MeasureTextNormalized, meaning it is measured with
    * font size 1.0f and relativeSpacing equal to SizeRelativeSpacing. The renderer applies the actual Size scaling itself. */
    Vector2 CachedDrawSize;
    /** @brief true if @c CachedDrawSize holds a valid cached measurement to reuse. */
    bool HasCachedDrawSize;
} TextRenderArguments;

/**
 * @brief The full set of parameters for drawing a 3D model through a render context.
 *
 * Positions, rotates, and scales the model in world space (there is no aspect-ratio fitting for 3D
 * geometry). Must be drawn inside a 3D pass opened with RenderContext_Begin3DMode.
 */
typedef struct ModelRenderArgumentsStruct
{
    /** @brief The model to draw; borrowed for the duration of the draw call. */
    Model TargetModel;
    /** @brief World-space position to place the model at. */
    Vector3 Position;
    /** @brief World-space axis to rotate the model around; need not be normalized. */
    Vector3 RotationAxis;
    /** @brief Rotation around @c RotationAxis, in radians. */
    float RotationAngleRad;
    /** @brief Per-axis world-space scale applied to the model. */
    Vector3 Scale;
    /** @brief The color/tint to draw with. */
    RenderColor TargetColor;
} ModelRenderArguments;

/**
 * @brief The full set of parameters for drawing a single 3D mesh through a render context.
 *
 * The mesh is drawn with an explicit world-space transform matrix and the given material; the material
 * carries its own colors (there is no separate tint). Must be drawn inside a 3D pass opened with
 * RenderContext_Begin3DMode.
 */
typedef struct MeshRenderArgumentsStruct
{
    /** @brief The mesh to draw; borrowed for the duration of the draw call. */
    Mesh TargetMesh;
    /** @brief The material to draw the mesh with; borrowed for the duration of the draw call. */
    Material TargetMaterial;
    /** @brief World-space transform (translation, rotation, scale) applied to the mesh. */
    Matrix Transform;
} MeshRenderArguments;

/**
 * @brief The full set of parameters for drawing many instances of one 3D mesh in a single call.
 *
 * Draws @c InstanceCount copies of @c TargetMesh, each with its own transform from @c Transforms, sharing
 * one material. Must be drawn inside a 3D pass opened with RenderContext_Begin3DMode.
 */
typedef struct MeshInstancedRenderArgumentsStruct
{
    /** @brief The mesh to draw; borrowed for the duration of the draw call. */
    Mesh TargetMesh;
    /** @brief The material shared by every instance; borrowed for the duration of the draw call. */
    Material TargetMaterial;
    /** @brief Array of @c InstanceCount world-space transforms, one per instance; borrowed. */
    const Matrix* Transforms;
    /** @brief Number of instances to draw; must not exceed the length of @c Transforms. */
    int InstanceCount;
} MeshInstancedRenderArguments;


// Functions.
/**
 * @brief Creates a pixel-space RenderFloat.
 * @param value The scalar in render-buffer pixels.
 * @returns A RenderFloat tagged RenderValueType_Pixel.
 */
static inline RenderFloat RenderFloat_Window(float value)
{
    return (RenderFloat)
    {
        .Value = value,
        .Type = RenderValueType_Pixel
    };
}

/**
 * @brief Creates a normalized-relative RenderFloat (fraction of the render buffer).
 * @param value The scalar as a [0;1] fraction of the render buffer.
 * @returns A RenderFloat tagged RenderValueType_NormalizedRelative.
 */
static inline RenderFloat RenderFloat_Relative(float value)
{
    return (RenderFloat)
    {
        .Value = value,
        .Type = RenderValueType_NormalizedRelative
    };
}

/**
 * @brief Creates a normalized-fitted RenderFloat (fraction of the target render area).
 * @param value The scalar as a [0;1] fraction of the target render area.
 * @returns A RenderFloat tagged RenderValueType_NormalizedFitted.
 */
static inline RenderFloat RenderFloat_Fitted(float value)
{
    return (RenderFloat)
    {
        .Value = value,
        .Type = RenderValueType_NormalizedFitted
    };
}

/**
 * @brief Creates a pixel-space RenderVector2D.
 * @param value The vector in render-buffer pixels.
 * @returns A RenderVector2D tagged RenderValueType_Pixel.
 */
static inline RenderVector2D RenderVector2D_Window(Vector2 value)
{
    return (RenderVector2D)
    {
        .Value = value,
        .Type = RenderValueType_Pixel
    };
}

/**
 * @brief Creates a normalized-relative RenderVector2D (fraction of the render buffer).
 * @param value The vector as [0;1] fractions of the render buffer.
 * @returns A RenderVector2D tagged RenderValueType_NormalizedRelative.
 */
static inline RenderVector2D RenderVector2D_Relative(Vector2 value)
{
    return (RenderVector2D)
    {
        .Value = value,
        .Type = RenderValueType_NormalizedRelative
    };
}

/**
 * @brief Creates a normalized-fitted RenderVector2D (fraction of the target render area).
 * @param value The vector as [0;1] fractions of the target render area.
 * @returns A RenderVector2D tagged RenderValueType_NormalizedFitted.
 */
static inline RenderVector2D RenderVector2D_Fitted(Vector2 value)
{
    return (RenderVector2D)
    {
        .Value = value,
        .Type = RenderValueType_NormalizedFitted
    };
}


/**
 * @brief Resolves a RenderColor into a concrete raylib color.
 *
 * Clamps opacity and brightness to their allowed ranges, sets the alpha from opacity, and scales the
 * tint's RGB by brightness. The tint's own alpha is ignored.
 * @param color The render color to resolve.
 * @returns The resulting raylib Color.
 */
Color RenderColor_GetFinalColor(RenderColor color);

/**
 * @brief Returns an opaque, full-brightness white render color.
 * @returns A RenderColor with white tint, brightness 1, and opacity 1.
 */
static inline RenderColor RenderColor_White(void)
{
    return (RenderColor)
    {
        .Tint = WHITE,
        .Brightness = 1.0f,
        .Opacity = 1.0f
    };
}

/**
 * @brief Returns an opaque black render color.
 *
 * Uses a white tint at zero brightness so the resolved RGB is black.
 * @returns A RenderColor with white tint, brightness 0, and opacity 1.
 */
static inline RenderColor RenderColor_Black(void)
{
    return (RenderColor)
    {
        .Tint = WHITE,
        .Brightness = 0.0f,
        .Opacity = 1.0f
    };
}

/**
 * @brief Returns a fully transparent render color.
 * @returns A RenderColor with white tint, brightness 1, and opacity 0.
 */
static inline RenderColor RenderColor_Transparent(void)
{
    return (RenderColor)
    {
        .Tint = WHITE,
        .Brightness = 1.0f,
        .Opacity = 0.0f
    };
}


/**
 * @brief Initializes a render context for the given target buffer and aspect-ratio fitting.
 *
 * Configures @p self to draw either to @p renderBuffer or, when it is NULL, to the window backbuffer,
 * captures the corresponding pixel size and aspect ratio, stores the fitting parameters, and resets the
 * draw counters. Any passed render buffer is borrowed and not owned.
 * @param self The render context to initialize; must not be NULL.
 * @param renderBuffer The custom render target, or NULL to target the window's backbuffer. Borrowed.
 * @param targetAspectRatio The aspect ratio (width / height) the game is designed for.
 * @param targetRelativePosition Normalized [0;1] placement of the target area within the render buffer.
 */
void RenderContext_Create(RenderContext* self,
    RenderTexture2D* renderBuffer,
    float targetAspectRatio,
    Vector2 targetRelativePosition);

/**
 * @brief Releases a render context without freeing its borrowed render buffer.
 *
 * Clears the context's fields. Any custom render buffer it referenced is left untouched.
 * @param self The render context to deconstruct; must not be NULL.
 */
void RenderContext_Deconstruct(RenderContext* self);

/**
 * @brief Returns the target-area placement that centers it within the render buffer.
 * @returns The normalized placement (0.5, 0.5).
 */
static inline Vector2 RenderTargetPosition_Centered(void)
{
    return (Vector2)
    {
        .x = 0.5f,
        .y = 0.5f
    };
}


/**
 * @brief Draws a texture through the render context using the given arguments.
 *
 * Converts the argument positions and sizes from their tagged coordinate spaces into pixels, applies the
 * origin, rotation, and resolved color, draws the texture, and increments the texture draw counter.
 * @param self The render context to draw with; must not be NULL.
 * @param args The texture draw parameters; must not be NULL.
 */
void RenderContext_RenderTexture2D(RenderContext* self, const TextureRenderArguments* args);

/**
 * @brief Draws a text string through the render context using the given arguments.
 *
 * Uses the cached draw size when @c args->HasCachedDrawSize is set, otherwise measures the text; then
 * converts the position and size into pixels, applies origin, spacing, rotation, and resolved color,
 * draws the text, and increments the string draw counter.
 * @param self The render context to draw with; must not be NULL.
 * @param args The text draw parameters; must not be NULL, and @c args->Text must be a valid UTF-8 string.
 */
void RenderContext_RenderText2D(RenderContext* self, const TextRenderArguments* args);

/**
 * @brief Draws a 3D model through the render context using the given arguments.
 *
 * Resolves the argument color, converts the rotation from radians to degrees, draws the model at the
 * requested world-space position/rotation/scale, and increments the model draw counter. Must be called
 * within a 3D pass opened by RenderContext_Begin3DMode.
 * @param self The render context to draw with; must not be NULL.
 * @param args The model draw parameters; must not be NULL.
 */
void RenderContext_RenderModel(RenderContext* self, const ModelRenderArguments* args);

/**
 * @brief Draws a single 3D mesh through the render context using the given arguments.
 *
 * Draws the mesh with the given material and world-space transform, and increments the mesh draw counter.
 * Must be called within a 3D pass opened by RenderContext_Begin3DMode.
 * @param self The render context to draw with; must not be NULL.
 * @param args The mesh draw parameters; must not be NULL.
 */
void RenderContext_RenderMesh(RenderContext* self, const MeshRenderArguments* args);

/**
 * @brief Draws many instances of one 3D mesh through the render context in a single call.
 *
 * Draws @c args->InstanceCount copies of the mesh, one per transform in @c args->Transforms, sharing the
 * given material, and increments the mesh draw counter by the instance count. Does nothing when the
 * instance count is not positive. Must be called within a 3D pass opened by RenderContext_Begin3DMode.
 * @param self The render context to draw with; must not be NULL.
 * @param args The instanced mesh draw parameters; must not be NULL, and @c args->Transforms must hold at
 *        least @c args->InstanceCount entries.
 */
void RenderContext_RenderMeshInstanced(RenderContext* self, const MeshInstancedRenderArguments* args);

/**
 * @brief Begins a 3D rendering pass with the given camera.
 *
 * Enters 3D mode so that subsequent model and mesh draws are projected through @p camera. Must be called
 * inside an active pass opened by RenderContext_BeginRendering, and paired with RenderContext_End3DMode.
 * @param self The render context; must not be NULL.
 * @param camera The camera defining the 3D view and projection.
 */
void RenderContext_Begin3DMode(RenderContext* self, Camera3D camera);

/**
 * @brief Ends the 3D rendering pass started by RenderContext_Begin3DMode.
 * @param self The render context; must not be NULL.
 */
void RenderContext_End3DMode(RenderContext* self);

/**
 * @brief Begins a rendering pass targeting this context's buffer.
 *
 * Starts texture mode on the custom render buffer, or the window drawing pass when there is no custom
 * buffer. Must be paired with RenderContext_EndRendering.
 * @param self The render context; must not be NULL.
 */
void RenderContext_BeginRendering(RenderContext* self);

/**
 * @brief Ends the rendering pass started by RenderContext_BeginRendering.
 * @param self The render context; must not be NULL.
 */
void RenderContext_EndRendering(RenderContext* self);


/**
 * @brief Converts a normalized-relative vector to render-buffer pixels.
 * @param self The render context; must not be NULL.
 * @param relativeCoords The [0;1] render-buffer-relative coordinates.
 * @returns The equivalent pixel coordinates.
 */
static inline Vector2 RenderContext_VectorRelativeToPixel(RenderContext* self, Vector2 relativeCoords)
{
    return (Vector2)
    {
        .x = relativeCoords.x * self->_renderBufferSizePixels.x,
        .y = relativeCoords.y * self->_renderBufferSizePixels.y,
    };
}

/**
 * @brief Converts a normalized-relative vector to normalized-fitted (target-area) coordinates.
 * @param self The render context; must not be NULL.
 * @param relativeCoords The [0;1] render-buffer-relative coordinates.
 * @param isOffsetIncluded true to account for the target area's offset within the render buffer (for
 *        positions); false for a pure scale (for sizes/deltas).
 * @returns The equivalent target-area coordinates.
 */
static inline Vector2 RenderContext_VectorRelativeToFitted(RenderContext* self, Vector2 relativeCoords, bool isOffsetIncluded)
{
    const float AspectRatioQuotient = self->_targetAspectRatio / self->_renderBufferAspectRatio;
    const float FittedWidth = (AspectRatioQuotient >= 1.0f) ? 1.0f : AspectRatioQuotient;
    const float FittedHeight = (AspectRatioQuotient >= 1.0f) ? (1.0f / AspectRatioQuotient) : 1.0f;

    Vector2 Result = relativeCoords;

    if (isOffsetIncluded)
    {
        Result.x -= (1.0f - FittedWidth) * self->_targetRelativePosition.x;
        Result.y -= (1.0f - FittedHeight) * self->_targetRelativePosition.y;
    }

    Result.x /= FittedWidth;
    Result.y /= FittedHeight;

    return Result;
}


/**
 * @brief Converts a render-buffer pixel vector to normalized-relative coordinates.
 * @param self The render context; must not be NULL.
 * @param pixelCoords The pixel coordinates.
 * @returns The equivalent [0;1] render-buffer-relative coordinates.
 */
static inline Vector2 RenderContext_VectorPixelToRelative(RenderContext* self, Vector2 pixelCoords)
{
    return (Vector2)
    {
        .x = pixelCoords.x / self->_renderBufferSizePixels.x,
        .y = pixelCoords.y / self->_renderBufferSizePixels.y
    };
}

/**
 * @brief Converts a render-buffer pixel vector to normalized-fitted (target-area) coordinates.
 * @param self The render context; must not be NULL.
 * @param pixelCoord The pixel coordinates.
 * @param isOffsetIncluded true to account for the target area's offset within the render buffer (for
 *        positions); false for a pure scale (for sizes/deltas).
 * @returns The equivalent target-area coordinates.
 */
static inline Vector2 RenderContext_VectorPixelToFitted(RenderContext* self, Vector2 pixelCoord, bool isOffsetIncluded)
{
    return RenderContext_VectorRelativeToFitted(self, RenderContext_VectorPixelToRelative(self, pixelCoord), isOffsetIncluded);
}



/**
 * @brief Converts a normalized-fitted (target-area) vector to normalized-relative coordinates.
 * @param self The render context; must not be NULL.
 * @param fittedCoords The [0;1] target-area coordinates.
 * @param isOffsetIncluded true to account for the target area's offset within the render buffer (for
 *        positions); false for a pure scale (for sizes/deltas).
 * @returns The equivalent [0;1] render-buffer-relative coordinates.
 */
static inline Vector2 RenderContext_VectorFittedToRelative(RenderContext* self, Vector2 fittedCoords, bool isOffsetIncluded)
{
    float AspectRatioQuotient = self->_targetAspectRatio / self->_renderBufferAspectRatio;
    float FittedWidth = (AspectRatioQuotient >= 1.0f) ? 1.0f : AspectRatioQuotient;
    float FittedHeight = (AspectRatioQuotient >= 1.0f) ? (1.0f / AspectRatioQuotient) : 1.0f;

    Vector2 Result;
    Result.x = fittedCoords.x * FittedWidth;
    Result.y = fittedCoords.y * FittedHeight;

    if (isOffsetIncluded)
    {
        Result.x += (1.0f - FittedWidth) * self->_targetRelativePosition.x;
        Result.y += (1.0f - FittedHeight) * self->_targetRelativePosition.y;
    }

    return Result;
}


/**
 * @brief Converts a normalized-fitted (target-area) vector to render-buffer pixels.
 * @param self The render context; must not be NULL.
 * @param fittedCoords The [0;1] target-area coordinates.
 * @param isOffsetIncluded true to account for the target area's offset within the render buffer (for
 *        positions); false for a pure scale (for sizes/deltas).
 * @returns The equivalent pixel coordinates.
 */
static inline Vector2 RenderContext_VectorFittedToPixel(RenderContext* self, Vector2 fittedCoords, bool isOffsetIncluded)
{
    Vector2 RelativeCoords = RenderContext_VectorFittedToRelative(self, fittedCoords, isOffsetIncluded);
    RelativeCoords.x *= self->_renderBufferSizePixels.x;
    RelativeCoords.y *= self->_renderBufferSizePixels.y;
    return RelativeCoords;
}


/**
 * @brief Converts a normalized-fitted (target-area) vertical size to a normalized-relative size.
 * @param self The render context; must not be NULL.
 * @param fittedSize The size as a fraction of the target area height.
 * @returns The equivalent size as a fraction of the render buffer height.
 */
static inline float RenderContext_SizeFittedToRelative(RenderContext* self, float fittedSize)
{
    const float AspectRatioQuotient = self->_targetAspectRatio / self->_renderBufferAspectRatio;
    const float FittedHeight = (AspectRatioQuotient >= 1.0f) ? (1.0f / AspectRatioQuotient) : 1.0f;
    return fittedSize * FittedHeight;
}

/**
 * @brief Converts a normalized-relative vertical size to a normalized-fitted (target-area) size.
 * @param self The render context; must not be NULL.
 * @param relativeSize The size as a fraction of the render buffer height.
 * @returns The equivalent size as a fraction of the target area height.
 */
static inline float RenderContext_SizeRelativeToFitted(RenderContext* self, float relativeSize)
{
    const float AspectRatioQuotient = self->_targetAspectRatio / self->_renderBufferAspectRatio;
    const float FittedHeight = (AspectRatioQuotient >= 1.0f) ? (1.0f / AspectRatioQuotient) : 1.0f;
    return relativeSize / FittedHeight;
}

/**
 * @brief Converts a normalized-relative vertical size to pixels.
 * @param self The render context; must not be NULL.
 * @param relativeSize The size as a fraction of the render buffer height.
 * @returns The equivalent size in pixels.
 */
static inline float RenderContext_SizeRelativeToPixel(RenderContext* self, float relativeSize)
{
    return relativeSize * self->_renderBufferSizePixels.y;
}

/**
 * @brief Converts a vertical size in pixels to a normalized-relative size.
 * @param self The render context; must not be NULL.
 * @param pixelSize The size in pixels.
 * @returns The equivalent size as a fraction of the render buffer height.
 */
static inline float RenderContext_SizePixelToRelative(RenderContext* self, float pixelSize)
{
    return pixelSize / self->_renderBufferSizePixels.y;
}

/**
 * @brief Converts a normalized-fitted (target-area) vertical size to pixels.
 * @param self The render context; must not be NULL.
 * @param fittedSize The size as a fraction of the target area height.
 * @returns The equivalent size in pixels.
 */
static inline float RenderContext_SizeFittedToPixel(RenderContext* self, float fittedSize)
{
    return RenderContext_SizeRelativeToPixel(self, RenderContext_SizeFittedToRelative(self, fittedSize));
}

/**
 * @brief Converts a vertical size in pixels to a normalized-fitted (target-area) size.
 * @param self The render context; must not be NULL.
 * @param pixelSize The size in pixels.
 * @returns The equivalent size as a fraction of the target area height.
 */
static inline float RenderContext_SizePixelToFitted(RenderContext* self, float pixelSize)
{
    return RenderContext_SizeRelativeToFitted(self, RenderContext_SizePixelToRelative(self, pixelSize));
}


/**
 * @brief Measures text at a normalized font size of 1.0 for later scaling/caching.
 *
 * Returns the size of @p text as measured with font size 1.0 and the given relative spacing, suitable
 * for storing in TextRenderArguments::CachedDrawSize; the renderer applies the actual font size itself.
 * @param font The font to measure with.
 * @param text The null-terminated UTF-8 text to measure.
 * @param relativeSpacing The character spacing multiplier to measure with.
 * @returns The measured text size at font size 1.0.
 */
static inline Vector2 Renderer_MeasureTextNormalized(Font font,
    const unsigned char* text,
    float relativeSpacing)
{
    return MeasureTextEx(font, (const char*)text, 1.0f, relativeSpacing);
}
