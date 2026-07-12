#include "TextComponentRenderer.h"
#include "wr/WRMemory.h"
#include <math.h>


// Macros.
/** Initial run-list capacity (elements); grows as needed and is reused across calls. */
#define TEXT_RENDERER_INITIAL_RUN_CAPACITY ((size_t)64)
/** Initial scratch byte-buffer capacity for NUL-terminated segment copies. */
#define TEXT_RENDERER_INITIAL_SCRATCH_CAPACITY ((size_t)128)
/** Factor the component brightness is multiplied by to derive the default shadow color. */
#define COMPONENT_SHADOW_DEFAULT_BRIGHTNESS_FACTOR (0.25f)
/** Underline/strikethrough thickness as a fraction of the string's font size. */
#define COMPONENT_DECORATION_RELATIVE_THICKNESS (0.06f)
/** Vertical position of the strikethrough within a run, as a fraction of the run height. */
#define COMPONENT_STRIKETHROUGH_RELATIVE_HEIGHT (0.5f)
/** Largest RGB channel value. */
#define COLOR_CHANNEL_MAX (255)


// Types.
// One laid-out piece of the flattened component: a text segment, a sprite box, or a forced line break.
// Widths/heights/positions are in normalized-fitted units (see the header); positions are filled by the
// layout pass.
typedef struct TextRunStruct
{
    /** true if this run only ends the current line (carries the line height, draws nothing). */
    bool IsLineBreak;
    /** true if this run is a sprite box; false if it is a text segment. */
    bool IsSprite;
    /** Run width in fitted units (0 for line breaks). */
    float Width;
    /** Run height in fitted units (contributes to the line height). */
    float Height;
    /** Block-local top-left X in fitted units, assigned by the layout pass. */
    float X;
    /** Block-local top-left Y in fitted units, assigned by the layout pass. */
    float Y;
    /** Owning string component for a text run; borrowed. */
    const StringComponent* StringComp;
    /** Start of the text segment within the component's text for a text run; borrowed. */
    const unsigned char* TextStart;
    /** Byte length of the text segment for a text run. */
    size_t TextLength;
    /** Owning sprite component for a sprite run; borrowed. */
    const SpriteComponent* SpriteComp;
} TextRun;


// Static functions.
static RenderColor MultiplyRenderColor(RenderColor a, RenderColor b)
{
    RenderColor Result;
    Result.Tint.r = (unsigned char)(((int)a.Tint.r * (int)b.Tint.r) / COLOR_CHANNEL_MAX);
    Result.Tint.g = (unsigned char)(((int)a.Tint.g * (int)b.Tint.g) / COLOR_CHANNEL_MAX);
    Result.Tint.b = (unsigned char)(((int)a.Tint.b * (int)b.Tint.b) / COLOR_CHANNEL_MAX);
    Result.Tint.a = (unsigned char)COLOR_CHANNEL_MAX; // Alpha ignored by RenderColor; opacity carries alpha.
    Result.Brightness = a.Brightness * b.Brightness;
    Result.Opacity = a.Opacity * b.Opacity;
    return Result;
}

static RenderColor DeriveDefaultShadowColor(RenderColor color)
{
    RenderColor Result = color;
    Result.Brightness = color.Brightness * COMPONENT_SHADOW_DEFAULT_BRIGHTNESS_FACTOR;
    return Result;
}

static Vector2 PositionToPixels(RenderContext* context, RenderVector2D position)
{
    switch (position.Type)
    {
        case RenderValueType_NormalizedRelative:
            return RenderContext_VectorRelativeToPixel(context, position.Value);
        case RenderValueType_NormalizedFitted:
            return RenderContext_VectorFittedToPixel(context, position.Value, true);
        case RenderValueType_Pixel:
        default:
            return position.Value;
    }
}

// Rotates a block-local pixel point around the origin and anchors it at the block base position.
static Vector2 PlacePoint(Vector2 localPixels, Vector2 originPixels, Vector2 basePixels, float cosValue, float sinValue)
{
    float OffsetX = localPixels.x - originPixels.x;
    float OffsetY = localPixels.y - originPixels.y;
    return (Vector2)
    {
        .x = basePixels.x + ((OffsetX * cosValue) - (OffsetY * sinValue)),
        .y = basePixels.y + ((OffsetX * sinValue) + (OffsetY * cosValue))
    };
}

// Copies a text segment into the scratch buffer as a NUL-terminated string and returns it. The returned
// pointer is valid until the next scratch use.
static const unsigned char* BuildSegmentCString(TextComponentRenderer* self, const unsigned char* start, size_t length)
{
    GenericBuffer_Clear(&self->_scratch);
    if (length > 0)
    {
        // AppendRangeBytes only reads the source; the API just isn't declared const.
        GenericBuffer_AppendRangeBytes(&self->_scratch, (unsigned char*)start, length);
    }
    GenericBuffer_NullTerminate(&self->_scratch);
    return self->_scratch._data;
}

// Measures a single text segment (no newlines) in fitted units.
static Vector2 MeasureSegment(TextComponentRenderer* self, const StringComponent* component,
    const unsigned char* start, size_t length)
{
    if (length == 0)
    {
        // Empty segment: zero width but a full line height so empty lines still take vertical space.
        return (Vector2){ .x = 0.0f, .y = component->_size };
    }

    const unsigned char* CString = BuildSegmentCString(self, start, length);
    Vector2 Measured = Renderer_MeasureTextNormalized(component->_font._rayFont, CString, component->_spacing);
    return (Vector2){ .x = Measured.x * component->_size, .y = Measured.y * component->_size };
}

static bool AppendRun(TextComponentRenderer* self, TextRun* run)
{
    return GenericBuffer_AddLast(&self->_runs, run);
}

static Error EmitTextRun(TextComponentRenderer* self, const StringComponent* component,
    const unsigned char* start, size_t length)
{
    Vector2 Size = MeasureSegment(self, component, start, length);

    TextRun Run = { 0 };
    Run.StringComp = component;
    Run.TextStart = start;
    Run.TextLength = length;
    Run.Width = Size.x;
    Run.Height = Size.y;
    if (!AppendRun(self, &Run))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "TextComponentRenderer: could not grow the run list.");
    }
    return Error_CreateSuccess();
}

static Error EmitLineBreak(TextComponentRenderer* self, const StringComponent* component)
{
    TextRun Run = { 0 };
    Run.IsLineBreak = true;
    Run.Height = component->_size;
    if (!AppendRun(self, &Run))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "TextComponentRenderer: could not grow the run list.");
    }
    return Error_CreateSuccess();
}

static Error EmitStringRuns(TextComponentRenderer* self, const StringComponent* component)
{
    const unsigned char* Text = component->_text;
    if (Text == NULL)
    {
        return Error_CreateSuccess();
    }

    size_t SegmentStart = 0;
    for (size_t Index = 0; ; Index++)
    {
        unsigned char Character = Text[Index];
        if (Character == 0)
        {
            // Emit a final non-empty segment; a trailing empty segment (after a newline) adds no line.
            if (Index > SegmentStart)
            {
                Error Result = EmitTextRun(self, component, Text + SegmentStart, Index - SegmentStart);
                if (Result.Code != ErrorCode_Success)
                {
                    return Result;
                }
            }
            break;
        }

        if (Character == (unsigned char)'\n')
        {
            Error Result = EmitTextRun(self, component, Text + SegmentStart, Index - SegmentStart);
            if (Result.Code != ErrorCode_Success)
            {
                return Result;
            }
            Result = EmitLineBreak(self, component);
            if (Result.Code != ErrorCode_Success)
            {
                return Result;
            }
            SegmentStart = Index + 1;
        }
    }

    return Error_CreateSuccess();
}

static Error EmitSpriteRun(TextComponentRenderer* self, const SpriteComponent* component)
{
    // A NULL animation instance renders nothing and reserves no space (acts empty).
    if (component->_animationInstance == NULL)
    {
        return Error_CreateSuccess();
    }

    TextRun Run = { 0 };
    Run.IsSprite = true;
    Run.SpriteComp = component;
    Run.Width = component->_size.x;
    Run.Height = component->_size.y;
    if (!AppendRun(self, &Run))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "TextComponentRenderer: could not grow the run list.");
    }
    return Error_CreateSuccess();
}

// Depth-first: a component's own content, then each child in order.
static Error BuildRuns(TextComponentRenderer* self, const TextComponent* component)
{
    Error Result = Error_CreateSuccess();

    switch (component->Type)
    {
        case TextComponentType_String:
            Result = EmitStringRuns(self, (const StringComponent*)component);
            break;
        case TextComponentType_Sprite:
            Result = EmitSpriteRun(self, (const SpriteComponent*)component);
            break;
        case TextComponentType_Empty:
        default:
            break;
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    size_t ChildCount = TextComponent_GetSubComponentCount(component);
    for (size_t Index = 0; Index < ChildCount; Index++)
    {
        const TextComponent* Child = TextComponent_GetSubComponentAt(component, Index);
        if (Child == NULL)
        {
            continue;
        }
        Result = BuildRuns(self, Child);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    return Error_CreateSuccess();
}

// Assigns each run's X/Y (bottom-aligned within its line) and returns the total block size, in fitted units.
static Vector2 LayoutRuns(TextComponentRenderer* self)
{
    size_t Count = self->_runs._count;
    if (Count == 0)
    {
        return (Vector2){ .x = 0.0f, .y = 0.0f };
    }

    TextRun* Runs = GenericBuffer_GetPointerToElement(&self->_runs, 0);
    float PenX = 0.0f;
    float PenY = 0.0f;
    float MaxWidth = 0.0f;
    float LineMaxHeight = 0.0f;
    size_t LineStart = 0;

    for (size_t Index = 0; Index < Count; Index++)
    {
        TextRun* Run = &Runs[Index];
        if (Run->IsLineBreak)
        {
            if (Run->Height > LineMaxHeight)
            {
                LineMaxHeight = Run->Height;
            }
            for (size_t Inner = LineStart; Inner < Index; Inner++)
            {
                if (!Runs[Inner].IsLineBreak)
                {
                    Runs[Inner].Y = PenY + (LineMaxHeight - Runs[Inner].Height);
                }
            }
            if (PenX > MaxWidth)
            {
                MaxWidth = PenX;
            }
            PenY += LineMaxHeight;
            PenX = 0.0f;
            LineMaxHeight = 0.0f;
            LineStart = Index + 1;
        }
        else
        {
            Run->X = PenX;
            PenX += Run->Width;
            if (Run->Height > LineMaxHeight)
            {
                LineMaxHeight = Run->Height;
            }
        }
    }

    // Finalize a trailing line only if it actually holds content.
    if ((PenX > 0.0f) || (LineMaxHeight > 0.0f))
    {
        for (size_t Inner = LineStart; Inner < Count; Inner++)
        {
            if (!Runs[Inner].IsLineBreak)
            {
                Runs[Inner].Y = PenY + (LineMaxHeight - Runs[Inner].Height);
            }
        }
        if (PenX > MaxWidth)
        {
            MaxWidth = PenX;
        }
        PenY += LineMaxHeight;
    }

    return (Vector2){ .x = MaxWidth, .y = PenY };
}

static void DrawTextAt(RenderContext* context, const StringComponent* component, const unsigned char* cString,
    Vector2 position, float fontSizePixels, float rotationRad, RenderColor color)
{
    TextRenderArguments Args = { 0 };
    Args.Text = cString;
    Args.TargetFont = component->_font._rayFont;
    Args.Position = RenderVector2D_Window(position);
    Args.Size = RenderFloat_Window(fontSizePixels);
    Args.SizeRelativeSpacing = component->_spacing;
    Args.RelativeOrigin = (Vector2){ .x = 0.0f, .y = 0.0f };
    Args.RotationRad = rotationRad;
    Args.TargetColor = color;
    Args.HasCachedDrawSize = false;
    RenderContext_RenderText2D(context, &Args);
}

static void DrawDecoration(RenderContext* context, float runXPixels, float runWidthPixels, float lineLocalYPixels,
    float thicknessPixels, Vector2 originPixels, Vector2 basePixels, float cosValue, float sinValue, RenderColor color)
{
    Vector2 StartLocal = { .x = runXPixels, .y = lineLocalYPixels };
    Vector2 EndLocal = { .x = runXPixels + runWidthPixels, .y = lineLocalYPixels };
    Vector2 StartPosition = PlacePoint(StartLocal, originPixels, basePixels, cosValue, sinValue);
    Vector2 EndPosition = PlacePoint(EndLocal, originPixels, basePixels, cosValue, sinValue);

    LineRenderArguments Args = { 0 };
    Args.StartPosition = RenderVector2D_Window(StartPosition);
    Args.EndPosition = RenderVector2D_Window(EndPosition);
    Args.Thickness = RenderFloat_Window(thicknessPixels);
    Args.TargetColor = color;
    RenderContext_RenderLine(context, &Args);
}

static void DrawTextRun(TextComponentRenderer* self, RenderContext* context, const ComponentRenderArguments* args,
    const TextRun* run, Vector2 originPixels, Vector2 basePixels, float cosValue, float sinValue)
{
    const StringComponent* Component = run->StringComp;
    if (run->TextLength == 0)
    {
        return; // Empty line spacer: nothing to draw.
    }

    float Multiplier = args->AdditionalSizeMultiplier;
    float RunXPixels = RenderContext_SizeFittedToPixel(context, run->X * Multiplier);
    float RunYPixels = RenderContext_SizeFittedToPixel(context, run->Y * Multiplier);
    float RunHeightPixels = RenderContext_SizeFittedToPixel(context, run->Height * Multiplier);
    float FontSizePixels = RenderContext_SizeFittedToPixel(context, Component->_size * Multiplier);
    const unsigned char* CString = BuildSegmentCString(self, run->TextStart, run->TextLength);
    RenderColor MainColor = MultiplyRenderColor(Component->_color, args->Tint);

    if (Component->IsShadowActive)
    {
        RenderColor ShadowBase = (Component->_shadowColorType == TextShadowColorType_Custom)
            ? Component->_shadowColor
            : DeriveDefaultShadowColor(Component->_color);
        RenderColor ShadowColor = MultiplyRenderColor(ShadowBase, args->Tint);

        float ShadowOffsetXPixels = RenderContext_SizeFittedToPixel(context,
            Component->_shadowOffset.x * Component->_size * Multiplier);
        float ShadowOffsetYPixels = RenderContext_SizeFittedToPixel(context,
            Component->_shadowOffset.y * Component->_size * Multiplier);
        Vector2 ShadowLocal = { .x = RunXPixels + ShadowOffsetXPixels, .y = RunYPixels + ShadowOffsetYPixels };
        Vector2 ShadowPosition = PlacePoint(ShadowLocal, originPixels, basePixels, cosValue, sinValue);
        DrawTextAt(context, Component, CString, ShadowPosition, FontSizePixels, args->RotationRad, ShadowColor);
    }

    Vector2 MainPosition = PlacePoint((Vector2){ .x = RunXPixels, .y = RunYPixels }, originPixels, basePixels, cosValue, sinValue);
    DrawTextAt(context, Component, CString, MainPosition, FontSizePixels, args->RotationRad, MainColor);

    float RunWidthPixels = RenderContext_SizeFittedToPixel(context, run->Width * Multiplier);
    float ThicknessPixels = FontSizePixels * COMPONENT_DECORATION_RELATIVE_THICKNESS;
    if (Component->IsUnderlined)
    {
        DrawDecoration(context, RunXPixels, RunWidthPixels, RunYPixels + RunHeightPixels, ThicknessPixels,
            originPixels, basePixels, cosValue, sinValue, MainColor);
    }
    if (Component->IsStrikethrough)
    {
        DrawDecoration(context, RunXPixels, RunWidthPixels, RunYPixels + (RunHeightPixels * COMPONENT_STRIKETHROUGH_RELATIVE_HEIGHT),
            ThicknessPixels, originPixels, basePixels, cosValue, sinValue, MainColor);
    }
}

static void DrawSpriteRun(RenderContext* context, const ComponentRenderArguments* args, const TextRun* run,
    Vector2 originPixels, Vector2 basePixels, float cosValue, float sinValue)
{
    const SpriteComponent* Component = run->SpriteComp;
    SpriteAnimationInstance* Instance = Component->_animationInstance;
    if (Instance == NULL)
    {
        return;
    }

    SpriteAnimationFrame Frame;
    Error FrameResult = SpriteAnimationInstance_GetCurrentFrame(Instance, &Frame);
    if (FrameResult.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&FrameResult);
        return;
    }

    float TextureWidth = (float)Frame._texture.width;
    float TextureHeight = (float)Frame._texture.height;
    if ((TextureWidth <= 0.0f) || (TextureHeight <= 0.0f))
    {
        return;
    }

    Rectangle RelativeSource =
    {
        .x = Frame._areaInTexture.x / TextureWidth,
        .y = Frame._areaInTexture.y / TextureHeight,
        .width = Frame._areaInTexture.width / TextureWidth,
        .height = Frame._areaInTexture.height / TextureHeight
    };

    float Multiplier = args->AdditionalSizeMultiplier;
    float RunXPixels = RenderContext_SizeFittedToPixel(context, run->X * Multiplier);
    float RunYPixels = RenderContext_SizeFittedToPixel(context, run->Y * Multiplier);
    float WidthPixels = RenderContext_SizeFittedToPixel(context, run->Width * Multiplier);
    float HeightPixels = RenderContext_SizeFittedToPixel(context, run->Height * Multiplier);
    Vector2 Position = PlacePoint((Vector2){ .x = RunXPixels, .y = RunYPixels }, originPixels, basePixels, cosValue, sinValue);

    TextureRenderArguments Args = { 0 };
    Args.Texture = Frame._texture;
    Args.Position = RenderVector2D_Window(Position);
    Args.RelativeSourceRectangle = RelativeSource;
    Args.Size = RenderVector2D_Window((Vector2){ .x = WidthPixels, .y = HeightPixels });
    Args.RelativeOrigin = (Vector2){ .x = 0.0f, .y = 0.0f };
    Args.RotationRad = args->RotationRad;
    Args.TargetColor = MultiplyRenderColor(Component->_color, args->Tint);
    RenderContext_RenderTexture2D(context, &Args);
}

static void DrawRuns(TextComponentRenderer* self, RenderContext* context, const ComponentRenderArguments* args,
    Vector2 blockSizeFitted)
{
    float Multiplier = args->AdditionalSizeMultiplier;
    float BlockWidthPixels = RenderContext_SizeFittedToPixel(context, blockSizeFitted.x * Multiplier);
    float BlockHeightPixels = RenderContext_SizeFittedToPixel(context, blockSizeFitted.y * Multiplier);
    Vector2 OriginPixels =
    {
        .x = BlockWidthPixels * args->RelativeOrigin.x,
        .y = BlockHeightPixels * args->RelativeOrigin.y
    };
    Vector2 BasePixels = PositionToPixels(context, args->Position);
    float CosValue = cosf(args->RotationRad);
    float SinValue = sinf(args->RotationRad);

    size_t Count = self->_runs._count;
    if (Count == 0)
    {
        return;
    }
    TextRun* Runs = GenericBuffer_GetPointerToElement(&self->_runs, 0);
    for (size_t Index = 0; Index < Count; Index++)
    {
        const TextRun* Run = &Runs[Index];
        if (Run->IsLineBreak)
        {
            continue;
        }
        if (Run->IsSprite)
        {
            DrawSpriteRun(context, args, Run, OriginPixels, BasePixels, CosValue, SinValue);
        }
        else
        {
            DrawTextRun(self, context, args, Run, OriginPixels, BasePixels, CosValue, SinValue);
        }
    }
}


// Public functions.
Error TextComponentRenderer_Construct(TextComponentRenderer* self)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "TextComponentRenderer_Construct: self must not be NULL.");
    }

    GenericBuffer_AllocateVariable(&self->_runs, TEXT_RENDERER_INITIAL_RUN_CAPACITY, sizeof(TextRun));
    GenericBuffer_AllocateVariable(&self->_scratch, TEXT_RENDERER_INITIAL_SCRATCH_CAPACITY, sizeof(unsigned char));
    return Error_CreateSuccess();
}

Error TextComponentRenderer_Deconstruct(TextComponentRenderer* self)
{
    if (self == NULL)
    {
        return Error_CreateSuccess();
    }

    Memory_Free(self->_runs._data);
    self->_runs._data = NULL;
    Memory_Free(self->_scratch._data);
    self->_scratch._data = NULL;
    return Error_CreateSuccess();
}

Error TextComponentRenderer_MeasureComponent(TextComponentRenderer* self, const TextComponent* component, Vector2* outSize)
{
    if ((self == NULL) || (component == NULL) || (outSize == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextComponentRenderer_MeasureComponent: self, component and outSize must not be NULL.");
    }
    *outSize = (Vector2){ .x = 0.0f, .y = 0.0f };

    GenericBuffer_Clear(&self->_runs);
    Error Result = BuildRuns(self, component);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    *outSize = LayoutRuns(self);
    return Error_CreateSuccess();
}

Error TextComponentRenderer_Render(TextComponentRenderer* self,
    RenderContext* context,
    const TextComponent* component,
    const ComponentRenderArguments* args)
{
    if ((self == NULL) || (context == NULL) || (component == NULL) || (args == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextComponentRenderer_Render: self, context, component and args must not be NULL.");
    }

    GenericBuffer_Clear(&self->_runs);
    Error Result = BuildRuns(self, component);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    // Layout always runs to assign run positions; a cached size only spares recomputing the block size.
    Vector2 BlockSize = LayoutRuns(self);
    if (args->HasCachedRenderSize)
    {
        BlockSize = args->CachedRenderSize;
    }

    DrawRuns(self, context, args, BlockSize);
    return Error_CreateSuccess();
}
