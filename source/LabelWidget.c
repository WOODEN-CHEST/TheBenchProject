#include "LabelWidget.h"
#include "UIWidget.h"
#include "UIScreen.h"
#include "UIWidgetFactory.h"
#include "UIRenderContext.h"
#include "TextComponent.h"
#include "TextComponentFactory.h"
#include "TextComponentRenderer.h"
#include "Renderer.h"
#include "raylib/raylib.h"
#include "wr/WRMemory.h"
#include "wr/WRMath.h"
#include "wr/WRCompile.h"


// Macros.
/** Sentinel line index for an atom dropped during wrapping (a wrapped-away boundary space). */
#define LABEL_ATOM_DROPPED GENERIC_BUFFER_INDEX_INVALID


// Types.
/* One flattened piece of the source text: a text token, a sprite box, or a hard line break.
 * Widths/heights are in normalized-fitted units at the component's own size (no label Size multiplier). */
typedef struct LabelAtomStruct
{
    bool IsSprite;
    bool IsSpace;                  // a whitespace text token (a break candidate)
    bool IsHardBreak;              // a '\n' that ends its line
    const StringComponent* Style;  // owning string component (text tokens and hard breaks)
    const SpriteComponent* Sprite; // owning sprite component (sprite atoms)
    float Width;                   // fitted width at the component size (0 for hard breaks)
    float Height;                  // fitted height at the component size
    size_t LineIndex;              // assigned line, or LABEL_ATOM_DROPPED
    size_t TextOffset;             // offset of the NUL-terminated token in _lineTextBuffer (text atoms)
} LabelAtom;

/* One built line: its rendered component subtree, resize multiplier, and measured (pre-resize) size. */
typedef struct LineRecordStruct
{
    TextComponent* Root;      // EmptyComponent + run children; owned (returned to the factory)
    float ResizeMultiplier;   // per-line shrink to fit bounds (Resize modes), else 1
    Vector2 MeasuredSize;     // fitted line size at component sizes (before resize and label Size)
} LineRecord;


// Static functions: small helpers.
static bool IsFloatFinite(float value)
{
    return !Math_IsNaNFloat(value) && !Math_IsInfinityFloat(value);
}

static bool IsVector2Finite(Vector2 value)
{
    return IsFloatFinite(value.x) && IsFloatFinite(value.y);
}

/* Keeps the first non-success error and releases any later one, for best-effort teardown loops. */
static void KeepFirstError(Error* first, Error* candidate)
{
    if ((candidate->Code != ErrorCode_Success) && (first->Code == ErrorCode_Success))
    {
        *first = *candidate;
    }
    else
    {
        Error_Deconstruct(candidate);
    }
}

static bool ShouldWrap(const LabelWidget* self)
{
    return self->_hasBounds
        && ((self->_boundHandling == LabelBoundHandling_Wrap)
            || (self->_boundHandling == LabelBoundHandling_WrapThenResize)
            || (self->_boundHandling == LabelBoundHandling_WrapThenCut));
}

static bool ShouldResize(const LabelWidget* self)
{
    return self->_hasBounds
        && ((self->_boundHandling == LabelBoundHandling_Resize)
            || (self->_boundHandling == LabelBoundHandling_WrapThenResize));
}

static bool ShouldCut(const LabelWidget* self)
{
    return self->_hasBounds
        && ((self->_boundHandling == LabelBoundHandling_Cut)
            || (self->_boundHandling == LabelBoundHandling_WrapThenCut));
}

/* Horizontal offset of a line within the block for the given alignment (fitted, draw units). */
static float AlignOffset(LabelAlignment alignment, float blockWidth, float lineWidth)
{
    switch (alignment)
    {
        case LabelAlignment_Center: return (blockWidth - lineWidth) * 0.5f;
        case LabelAlignment_Right:  return blockWidth - lineWidth;
        case LabelAlignment_Left:
        default:                    return 0.0f;
    }
}

/* Converts a fitted-units vector to a screen-relative [0;1] vector via the render context. */
static Vector2 FittedToScreenRel(RenderContext* context, Vector2 fitted)
{
    float PixelX = RenderContext_SizeFittedToPixel(context, fitted.x);
    float PixelY = RenderContext_SizeFittedToPixel(context, fitted.y);
    return RenderContext_VectorPixelToRelative(context, (Vector2){ .x = PixelX, .y = PixelY });
}

/* Product of the widget's ancestor sizes (its parent's absolute screen size); {1,1} for a root. */
static Vector2 ComputeParentAbsoluteSize(Widget* widget)
{
    Vector2 Size = { .x = 1.0f, .y = 1.0f };
    Widget* Parent = Widget_GetParent(widget);
    while (Parent != NULL)
    {
        Vector2 ParentSize = Widget_GetSize(Parent);
        Size.x *= ParentSize.x;
        Size.y *= ParentSize.y;
        Parent = Widget_GetParent(Parent);
    }
    return Size;
}

/* Copies every style property from @p src onto @p dst (via setters / public flags). */
static Error CopyStringStyle(StringComponent* dst, const StringComponent* src)
{
    Error Result = StringComponent_SetFont(dst, StringComponent_GetFont(src));
    if (Result.Code == ErrorCode_Success) { Result = StringComponent_SetFontName(dst, StringComponent_GetFontName(src)); }
    if (Result.Code == ErrorCode_Success) { Result = StringComponent_SetColor(dst, StringComponent_GetColor(src)); }
    if (Result.Code == ErrorCode_Success) { Result = StringComponent_SetSize(dst, StringComponent_GetSize(src)); }
    if (Result.Code == ErrorCode_Success) { Result = StringComponent_SetSpacing(dst, StringComponent_GetSpacing(src)); }
    if (Result.Code == ErrorCode_Success) { Result = StringComponent_SetShadowOffset(dst, StringComponent_GetShadowOffset(src)); }
    if (Result.Code == ErrorCode_Success)
    {
        if (StringComponent_GetShadowColorType(src) == TextShadowColorType_Custom)
        {
            Result = StringComponent_SetShadowColorCustom(dst, StringComponent_GetShadowColor(src));
        }
        else
        {
            Result = StringComponent_SetShadowColorDefault(dst);
        }
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    dst->IsShadowActive = src->IsShadowActive;
    dst->IsUnderlined = src->IsUnderlined;
    dst->IsStrikethrough = src->IsStrikethrough;
    return Error_CreateSuccess();
}


// Static functions: flatten the source text into atoms.
static Error EmitTextAtom(LabelWidget* self, const StringComponent* style, GameFont font, float spacing, float size,
    const unsigned char* start, size_t length, bool isSpace)
{
    size_t Offset = self->_lineTextBuffer._count;
    if (!GenericBuffer_AppendRangeBytes(&self->_lineTextBuffer, (unsigned char*)start, length)
        || !GenericBuffer_AppendByte(&self->_lineTextBuffer, 0))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "LabelWidget: line text buffer could not grow.");
    }

    // The buffer is stable at this point (post-append); safe to measure the NUL-terminated token.
    const unsigned char* CString = self->_lineTextBuffer._data + Offset;
    Vector2 Measured = Renderer_MeasureTextNormalized(font._rayFont, CString, spacing);

    LabelAtom Atom;
    Memory_Zero(&Atom, sizeof(Atom));
    Atom.IsSpace = isSpace;
    Atom.Style = style;
    Atom.Width = Measured.x * size;
    Atom.Height = Measured.y * size;
    Atom.TextOffset = Offset;
    if (!GenericBuffer_AddLast(&self->_buildAtoms, &Atom))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "LabelWidget: atom buffer could not grow.");
    }
    return Error_CreateSuccess();
}

static Error EmitHardBreak(LabelWidget* self, const StringComponent* style, float size)
{
    LabelAtom Atom;
    Memory_Zero(&Atom, sizeof(Atom));
    Atom.IsHardBreak = true;
    Atom.Style = style;
    Atom.Height = size;
    if (!GenericBuffer_AddLast(&self->_buildAtoms, &Atom))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "LabelWidget: atom buffer could not grow.");
    }
    return Error_CreateSuccess();
}

static Error EmitSpriteAtom(LabelWidget* self, const SpriteComponent* sprite)
{
    Vector2 SpriteSize = SpriteComponent_GetSize(sprite);
    LabelAtom Atom;
    Memory_Zero(&Atom, sizeof(Atom));
    Atom.IsSprite = true;
    Atom.Sprite = sprite;
    Atom.Width = SpriteSize.x;
    Atom.Height = SpriteSize.y;
    if (!GenericBuffer_AddLast(&self->_buildAtoms, &Atom))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "LabelWidget: atom buffer could not grow.");
    }
    return Error_CreateSuccess();
}

/* Emits alternating word/space tokens from a newline-free text segment [from, to). */
static Error EmitTokens(LabelWidget* self, const StringComponent* style, GameFont font, float spacing, float size,
    const unsigned char* text, size_t from, size_t to)
{
    size_t TokenStart = from;
    while (TokenStart < to)
    {
        bool IsSpace = (text[TokenStart] == (unsigned char)' ');
        size_t TokenEnd = TokenStart;
        while ((TokenEnd < to) && ((text[TokenEnd] == (unsigned char)' ') == IsSpace))
        {
            TokenEnd++;
        }
        Error Result = EmitTextAtom(self, style, font, spacing, size, text + TokenStart, TokenEnd - TokenStart, IsSpace);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        TokenStart = TokenEnd;
    }
    return Error_CreateSuccess();
}

static Error EmitStringAtoms(LabelWidget* self, const StringComponent* component)
{
    const unsigned char* Text = StringComponent_GetText(component);
    if (Text == NULL)
    {
        return Error_CreateSuccess();
    }

    GameFont Font = StringComponent_GetFont(component);
    float Spacing = StringComponent_GetSpacing(component);
    float Size = StringComponent_GetSize(component);

    size_t SegmentStart = 0;
    size_t Index = 0;
    while (true)
    {
        unsigned char Character = Text[Index];
        if ((Character == 0) || (Character == (unsigned char)'\n'))
        {
            Error Result = EmitTokens(self, component, Font, Spacing, Size, Text, SegmentStart, Index);
            if (Result.Code != ErrorCode_Success)
            {
                return Result;
            }
            if (Character == 0)
            {
                break;
            }
            Result = EmitHardBreak(self, component, Size);
            if (Result.Code != ErrorCode_Success)
            {
                return Result;
            }
            SegmentStart = Index + 1;
        }
        Index++;
    }
    return Error_CreateSuccess();
}

static Error FlattenComponent(LabelWidget* self, const TextComponent* component)
{
    Error Result = Error_CreateSuccess();
    switch (TextComponent_GetType(component))
    {
        case TextComponentType_String:
            Result = EmitStringAtoms(self, (const StringComponent*)component);
            break;
        case TextComponentType_Sprite:
        {
            const SpriteComponent* Sprite = (const SpriteComponent*)component;
            if (SpriteComponent_GetAnimationInstance(Sprite) != NULL)
            {
                Result = EmitSpriteAtom(self, Sprite);
            }
            break;
        }
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
        Result = FlattenComponent(self, Child);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    return Error_CreateSuccess();
}


// Static functions: greedy line assignment and per-line metrics.
static void AssignLines(LabelWidget* self)
{
    bool DoWrap = ShouldWrap(self);
    float BoundsWidth = self->_bounds.x; // fitted draw units; text width already includes the Size multiplier
    size_t Count = self->_buildAtoms._count;

    size_t Line = 0;
    float LineWidthDraw = 0.0f;
    bool HasContent = false;
    size_t LastAtomInLine = GENERIC_BUFFER_INDEX_INVALID;

    for (size_t Index = 0; Index < Count; Index++)
    {
        LabelAtom* Atom = GenericBuffer_GetPointerToElement(&self->_buildAtoms, Index);

        if (Atom->IsHardBreak)
        {
            Atom->LineIndex = Line;
            Line++;
            LineWidthDraw = 0.0f;
            HasContent = false;
            LastAtomInLine = GENERIC_BUFFER_INDEX_INVALID;
            continue;
        }

        if (Atom->IsSpace && !HasContent)
        {
            Atom->LineIndex = LABEL_ATOM_DROPPED; // no leading spaces on a line
            continue;
        }

        float WidthDraw = Atom->Width * self->_size;
        bool CanBreakHere = (LastAtomInLine != GENERIC_BUFFER_INDEX_INVALID)
            && ((LabelAtom*)GenericBuffer_GetPointerToElement(&self->_buildAtoms, LastAtomInLine))->IsSpace;
        if (DoWrap && HasContent && !Atom->IsSpace && CanBreakHere && ((LineWidthDraw + WidthDraw) > BoundsWidth))
        {
            // Wrap at the space boundary: drop the trailing space and start a new line.
            LabelAtom* Last = GenericBuffer_GetPointerToElement(&self->_buildAtoms, LastAtomInLine);
            Last->LineIndex = LABEL_ATOM_DROPPED;
            Line++;
            LineWidthDraw = 0.0f;
            HasContent = false;
            LastAtomInLine = GENERIC_BUFFER_INDEX_INVALID;
        }

        Atom->LineIndex = Line;
        LineWidthDraw += WidthDraw;
        HasContent = true;
        LastAtomInLine = Index;
    }
}

static Error ComputeLineMeta(LabelWidget* self)
{
    size_t Count = self->_buildAtoms._count;

    // The last line that actually holds content; trailing empty lines (after a final newline) are dropped.
    size_t MaxContentLine = 0;
    bool HasAnyContent = false;
    for (size_t Index = 0; Index < Count; Index++)
    {
        LabelAtom* Atom = GenericBuffer_GetPointerToElement(&self->_buildAtoms, Index);
        if (!Atom->IsHardBreak && (Atom->LineIndex != LABEL_ATOM_DROPPED))
        {
            if (!HasAnyContent || (Atom->LineIndex > MaxContentLine))
            {
                MaxContentLine = Atom->LineIndex;
                HasAnyContent = true;
            }
        }
    }
    if (!HasAnyContent)
    {
        self->_drawSizeFitted = (Vector2){ .x = 0.0f, .y = 0.0f };
        return Error_CreateSuccess();
    }

    float BlockWidth = 0.0f;
    float BlockHeight = 0.0f;
    for (size_t Line = 0; Line <= MaxContentLine; Line++)
    {
        float Width = 0.0f;
        float Height = 0.0f;
        for (size_t Index = 0; Index < Count; Index++)
        {
            LabelAtom* Atom = GenericBuffer_GetPointerToElement(&self->_buildAtoms, Index);
            if (Atom->LineIndex != Line)
            {
                continue;
            }
            if (Atom->Height > Height)
            {
                Height = Atom->Height;
            }
            if (!Atom->IsHardBreak)
            {
                Width += Atom->Width;
            }
        }

        float Resize = 1.0f;
        if (ShouldResize(self) && (Width > 0.0f))
        {
            float WidthDraw = Width * self->_size;
            if ((self->_bounds.x > 0.0f) && (WidthDraw > self->_bounds.x))
            {
                Resize = self->_bounds.x / WidthDraw;
            }
        }

        LineRecord Record = { .Root = NULL, .ResizeMultiplier = Resize, .MeasuredSize = { .x = Width, .y = Height } };
        if (!GenericBuffer_AddLast(&self->_lines, &Record))
        {
            return Error_Construct2(ErrorCode_BufferTooLarge, "LabelWidget: line list could not grow.");
        }

        float LineWidth = Width * Resize;
        float LineHeight = Height * Resize;
        if (LineWidth > BlockWidth)
        {
            BlockWidth = LineWidth;
        }
        BlockHeight += LineHeight;
    }

    self->_drawSizeFitted = (Vector2){ .x = BlockWidth, .y = BlockHeight };
    return Error_CreateSuccess();
}


// Static functions: build the per-line component subtrees, and cache management.
static Error ReturnAllLines(LabelWidget* self)
{
    Error FirstError = Error_CreateSuccess();
    for (size_t Index = 0; Index < self->_lines._count; Index++)
    {
        LineRecord* Record = GenericBuffer_GetPointerToElement(&self->_lines, Index);
        if (Record->Root != NULL)
        {
            Error Result = TextComponentFactory_ReturnComponentTree(self->_componentFactory, Record->Root);
            KeepFirstError(&FirstError, &Result);
            Record->Root = NULL;
        }
    }
    return FirstError;
}

static Error BuildLineComponents(LabelWidget* self)
{
    size_t LineCount = self->_lines._count;
    size_t AtomCount = self->_buildAtoms._count;

    for (size_t Line = 0; Line < LineCount; Line++)
    {
        EmptyComponent* Root = NULL;
        Error Result = TextComponentFactory_CreateEmpty(self->_componentFactory, &Root);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }

        for (size_t Index = 0; Index < AtomCount; Index++)
        {
            LabelAtom* Atom = GenericBuffer_GetPointerToElement(&self->_buildAtoms, Index);
            if ((Atom->LineIndex != Line) || Atom->IsHardBreak)
            {
                continue;
            }

            TextComponent* Child = NULL;
            if (Atom->IsSprite)
            {
                SpriteComponent* Sprite = NULL;
                Result = TextComponentFactory_CreateSpriteSized(self->_componentFactory,
                    SpriteComponent_GetAnimationInstance(Atom->Sprite),
                    SpriteComponent_GetColor(Atom->Sprite),
                    SpriteComponent_GetSize(Atom->Sprite),
                    &Sprite);
                Child = (Result.Code == ErrorCode_Success) ? &Sprite->Base : NULL;
            }
            else
            {
                StringComponent* String = NULL;
                Result = TextComponentFactory_CreateString(self->_componentFactory, NULL, &String);
                if (Result.Code == ErrorCode_Success)
                {
                    Result = CopyStringStyle(String, Atom->Style);
                }
                if (Result.Code == ErrorCode_Success)
                {
                    Result = StringComponent_SetText(String, self->_lineTextBuffer._data + Atom->TextOffset);
                }
                Child = (Result.Code == ErrorCode_Success) ? &String->Base : NULL;
                if ((Result.Code != ErrorCode_Success) && (String != NULL))
                {
                    Error Cleanup = TextComponentFactory_ReturnComponentTree(self->_componentFactory, &String->Base);
                    Error_Deconstruct(&Cleanup);
                }
            }

            if (Result.Code != ErrorCode_Success)
            {
                Error Cleanup = TextComponentFactory_ReturnComponentTree(self->_componentFactory, &Root->Base);
                Error_Deconstruct(&Cleanup);
                return Result;
            }

            Result = TextComponent_AddSubComponent(&Root->Base, Child);
            if (Result.Code != ErrorCode_Success)
            {
                Error ChildCleanup = TextComponentFactory_ReturnComponentTree(self->_componentFactory, Child);
                Error_Deconstruct(&ChildCleanup);
                Error RootCleanup = TextComponentFactory_ReturnComponentTree(self->_componentFactory, &Root->Base);
                Error_Deconstruct(&RootCleanup);
                return Result;
            }
        }

        LineRecord* Record = GenericBuffer_GetPointerToElement(&self->_lines, Line);
        Record->Root = &Root->Base;
    }
    return Error_CreateSuccess();
}

static Error BuildCache(LabelWidget* self)
{
    Error ReturnResult = ReturnAllLines(self);
    GenericBuffer_Clear(&self->_lines);
    GenericBuffer_Clear(&self->_lineTextBuffer);
    GenericBuffer_Clear(&self->_buildAtoms);
    self->_drawSizeFitted = (Vector2){ .x = 0.0f, .y = 0.0f };
    self->_isCacheValid = false;
    if (ReturnResult.Code != ErrorCode_Success)
    {
        return ReturnResult;
    }

    if (self->_text != NULL)
    {
        Error FlattenResult = FlattenComponent(self, self->_text);
        if (FlattenResult.Code != ErrorCode_Success)
        {
            return FlattenResult;
        }
    }

    AssignLines(self);

    Error MetaResult = ComputeLineMeta(self);
    if (MetaResult.Code != ErrorCode_Success)
    {
        return MetaResult;
    }

    Error BuildResult = BuildLineComponents(self);
    if (BuildResult.Code != ErrorCode_Success)
    {
        Error Cleanup = ReturnAllLines(self);
        Error_Deconstruct(&Cleanup);
        GenericBuffer_Clear(&self->_lines);
        return BuildResult;
    }

    self->_isCacheValid = true;
    return Error_CreateSuccess();
}

/* Ensures the cache is built; a no-op when already valid. */
static Error EnsureCache(LabelWidget* self)
{
    if (self->_isCacheValid)
    {
        return Error_CreateSuccess();
    }
    return BuildCache(self);
}


// Static functions: rendering.
/* Writes the label's widget box size to match the drawn text block (auto-fit), so hit-testing and the
 * keyboard-navigation outline wrap the text. Position (the anchor) is left untouched. */
static Error AutoFitBox(LabelWidget* self, RenderContext* context, Vector2 drawSizeFitted)
{
    Vector2 BoxScreen = FittedToScreenRel(context, drawSizeFitted);
    Vector2 ParentAbsolute = ComputeParentAbsoluteSize(&self->Base);
    if ((ParentAbsolute.x <= 0.0f) || (ParentAbsolute.y <= 0.0f))
    {
        return Error_CreateSuccess();
    }
    Vector2 RelativeSize = { .x = BoxScreen.x / ParentAbsolute.x, .y = BoxScreen.y / ParentAbsolute.y };
    return Widget_SetSize(&self->Base, RelativeSize);
}

static Error LabelWidget_RenderVT(void* self, UIRenderContext* context)
{
    LabelWidget* Label = self;

    Error CacheResult = EnsureCache(Label);
    if (CacheResult.Code != ErrorCode_Success)
    {
        return CacheResult;
    }

    RenderContext* Context = UIRenderContext_GetRenderContext(context);
    Vector2 Anchor = UIRenderContext_LocalToScreen(context, (Vector2){ .x = 0.0f, .y = 0.0f });
    RenderColor Tint = UIRenderColor_Multiply(UIRenderContext_GetTint(context), Label->_tint);

    Vector2 DrawSize = { .x = Label->_drawSizeFitted.x * Label->_size, .y = Label->_drawSizeFitted.y * Label->_size };
    Vector2 Origin = { .x = Label->_origin.x * DrawSize.x, .y = Label->_origin.y * DrawSize.y };
    float CosValue = Math_CosFloat(Label->_rotation);
    float SinValue = Math_SinFloat(Label->_rotation);

    bool HasScissor = false;
    Rectangle Scissor = { 0 };
    if (ShouldCut(Label))
    {
        Vector2 BoundsScreen = FittedToScreenRel(Context, Label->_bounds);
        Scissor = (Rectangle){ .x = Anchor.x, .y = Anchor.y, .width = BoundsScreen.x, .height = BoundsScreen.y };
        HasScissor = true;
    }

    float PenY = 0.0f;
    size_t LineCount = Label->_lines._count;
    for (size_t Line = 0; Line < LineCount; Line++)
    {
        LineRecord* Record = GenericBuffer_GetPointerToElement(&Label->_lines, Line);
        float LineWidth = Record->MeasuredSize.x * Record->ResizeMultiplier * Label->_size;
        float LineHeight = Record->MeasuredSize.y * Record->ResizeMultiplier * Label->_size;
        float PenX = AlignOffset(Label->_alignment, DrawSize.x, LineWidth);

        // Rotate the line's block-local top-left around the origin, then anchor at the widget box.
        Vector2 Relative = { .x = PenX - Origin.x, .y = PenY - Origin.y };
        Vector2 Rotated = { .x = (Relative.x * CosValue) - (Relative.y * SinValue), .y = (Relative.x * SinValue) + (Relative.y * CosValue) };
        Vector2 OffsetFitted = { .x = Rotated.x + Origin.x, .y = Rotated.y + Origin.y };
        Vector2 OffsetRelative = FittedToScreenRel(Context, OffsetFitted);
        Vector2 Position = { .x = Anchor.x + OffsetRelative.x, .y = Anchor.y + OffsetRelative.y };

        if (Record->Root != NULL)
        {
            ComponentRenderArguments Arguments = ComponentRenderArguments_Create(RenderVector2D_Relative(Position));
            Arguments.AdditionalSizeMultiplier = Label->_size * Record->ResizeMultiplier;
            Arguments.Tint = Tint;
            Arguments.RotationRad = Label->_rotation;
            Arguments.HasScissor = HasScissor;
            Arguments.ScissorRelative = Scissor;
            Error DrawResult = TextComponentRenderer_Render(Label->_componentRenderer, Context, Record->Root, &Arguments);
            if (DrawResult.Code != ErrorCode_Success)
            {
                return DrawResult;
            }
        }

        PenY += LineHeight;
    }

    return AutoFitBox(Label, Context, DrawSize);
}

static Error LabelWidget_OnDeconstructVT(void* self)
{
    LabelWidget* Label = self;
    Error FirstError = ReturnAllLines(Label);
    Memory_Free(Label->_lines._data);
    Memory_Free(Label->_lineTextBuffer._data);
    Memory_Free(Label->_buildAtoms._data);
    return FirstError;
}

static Error LabelWidget_GetPropertyVT(void* self, int32_t propertyId, void* outValue)
{
    LabelWidget* Label = self;
    switch (propertyId)
    {
        case LabelProperty_Size:     *(float*)outValue = Label->_size; return Error_CreateSuccess();
        case LabelProperty_Rotation: *(float*)outValue = Label->_rotation; return Error_CreateSuccess();
        case LabelProperty_Tint:     *(RenderColor*)outValue = Label->_tint; return Error_CreateSuccess();
        case LabelProperty_Origin:   *(Vector2*)outValue = Label->_origin; return Error_CreateSuccess();
        default:
            return Error_Construct2(ErrorCode_InvalidOperation, "LabelWidget_GetProperty: unknown property id.");
    }
}

static Error LabelWidget_SetPropertyVT(void* self, int32_t propertyId, const void* value)
{
    LabelWidget* Label = self;
    switch (propertyId)
    {
        case LabelProperty_Size:     return LabelWidget_SetSize(Label, *(const float*)value);
        case LabelProperty_Rotation: return LabelWidget_SetRotation(Label, *(const float*)value);
        case LabelProperty_Tint:     return LabelWidget_SetTint(Label, *(const RenderColor*)value);
        case LabelProperty_Origin:   return LabelWidget_SetOrigin(Label, *(const Vector2*)value);
        default:
            return Error_Construct2(ErrorCode_InvalidOperation, "LabelWidget_SetProperty: unknown property id.");
    }
}


// Fields.
static const WidgetVTable LabelWidgetVTable =
{
    .Render = LabelWidget_RenderVT,
    .OnDeconstruct = LabelWidget_OnDeconstructVT,
    .GetProperty = LabelWidget_GetPropertyVT,
    .SetProperty = LabelWidget_SetPropertyVT
};


// Static functions: the widget constructor (registered with the factory).
static Error LabelWidget_ConstructVT(void* memory, UIScreen* screen, uint64_t typeId, void* args)
{
    LabelWidget* Label = memory;
    LabelWidgetArgs* Arguments = args;
    if ((Arguments == NULL) || (Arguments->ComponentFactory == NULL) || (Arguments->ComponentRenderer == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "LabelWidget: ComponentFactory and ComponentRenderer args are required.");
    }

    Error BaseResult = Widget_Construct(&Label->Base, &LabelWidgetVTable, screen, typeId);
    if (BaseResult.Code != ErrorCode_Success)
    {
        return BaseResult;
    }

    Label->_componentFactory = Arguments->ComponentFactory;
    Label->_componentRenderer = Arguments->ComponentRenderer;
    Label->_text = Arguments->Text;
    Label->_alignment = LabelAlignment_Left;
    Label->_origin = (Vector2){ .x = 0.0f, .y = 0.0f };
    Label->_rotation = 0.0f;
    Label->_size = 1.0f;
    Label->_tint = RenderColor_White();
    Label->_hasBounds = false;
    Label->_bounds = (Vector2){ .x = 0.0f, .y = 0.0f };
    Label->_boundHandling = LabelBoundHandling_Cut;
    Label->_isCacheValid = false;

    GenericBuffer_AllocateVariable(&Label->_lines, 4U, sizeof(LineRecord));
    GenericBuffer_AllocateVariable(&Label->_lineTextBuffer, 64U, sizeof(unsigned char));
    GenericBuffer_AllocateVariable(&Label->_buildAtoms, 16U, sizeof(LabelAtom));
    Label->_drawSizeFitted = (Vector2){ .x = 0.0f, .y = 0.0f };
    return Error_CreateSuccess();
}


// Public functions: registration and creation.
Error LabelWidget_RegisterType(UIWidgetFactory* factory, uint64_t* outTypeId)
{
    if ((factory == NULL) || (outTypeId == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "LabelWidget_RegisterType: factory and outTypeId must not be NULL.");
    }
    return UIWidgetFactory_RegisterType(factory, sizeof(LabelWidget), LabelWidget_ConstructVT, NULL, 0, outTypeId);
}

Error LabelWidget_Create(UIScreen* screen,
    uint64_t typeId,
    TextComponentFactory* componentFactory,
    TextComponentRenderer* componentRenderer,
    TextComponent* text,
    LabelWidget** outLabel)
{
    if ((screen == NULL) || (componentFactory == NULL) || (componentRenderer == NULL) || (outLabel == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "LabelWidget_Create: screen, componentFactory, componentRenderer and outLabel must not be NULL.");
    }
    *outLabel = NULL;

    LabelWidgetArgs Arguments = { .ComponentFactory = componentFactory, .ComponentRenderer = componentRenderer, .Text = text };
    Widget* Created = NULL;
    Error Result = UIWidgetFactory_ConstructWidget(UIScreen_GetFactory(screen), screen, typeId, &Arguments, &Created);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    *outLabel = (LabelWidget*)Created;
    return Error_CreateSuccess();
}


// Public functions: properties.
TextComponent* LabelWidget_GetText(const LabelWidget* self)
{
    return self->_text;
}

Error LabelWidget_SetText(LabelWidget* self, TextComponent* text)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "LabelWidget_SetText: self must not be NULL.");
    }
    self->_text = text;
    self->_isCacheValid = false;
    return Error_CreateSuccess();
}

LabelAlignment LabelWidget_GetAlignment(const LabelWidget* self)
{
    return self->_alignment;
}

Error LabelWidget_SetAlignment(LabelWidget* self, LabelAlignment alignment)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "LabelWidget_SetAlignment: self must not be NULL.");
    }
    if ((alignment != LabelAlignment_Left) && (alignment != LabelAlignment_Center) && (alignment != LabelAlignment_Right))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "LabelWidget_SetAlignment: alignment out of range.");
    }
    self->_alignment = alignment; // alignment is applied at render time; the cache does not depend on it
    return Error_CreateSuccess();
}

Vector2 LabelWidget_GetOrigin(const LabelWidget* self)
{
    return self->_origin;
}

Error LabelWidget_SetOrigin(LabelWidget* self, Vector2 origin)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "LabelWidget_SetOrigin: self must not be NULL.");
    }
    if (!IsVector2Finite(origin))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "LabelWidget_SetOrigin: origin components must be finite.");
    }
    self->_origin = origin;
    return Error_CreateSuccess();
}

Error LabelWidget_SetOriginPosition(LabelWidget* self, LabelOriginPosition position)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "LabelWidget_SetOriginPosition: self must not be NULL.");
    }

    float X;
    float Y;
    switch (position)
    {
        case LabelOriginPosition_TopLeft:      X = 0.0f; Y = 0.0f; break;
        case LabelOriginPosition_TopCenter:    X = 0.5f; Y = 0.0f; break;
        case LabelOriginPosition_TopRight:     X = 1.0f; Y = 0.0f; break;
        case LabelOriginPosition_MiddleLeft:   X = 0.0f; Y = 0.5f; break;
        case LabelOriginPosition_MiddleCenter: X = 0.5f; Y = 0.5f; break;
        case LabelOriginPosition_MiddleRight:  X = 1.0f; Y = 0.5f; break;
        case LabelOriginPosition_BottomLeft:   X = 0.0f; Y = 1.0f; break;
        case LabelOriginPosition_BottomCenter: X = 0.5f; Y = 1.0f; break;
        case LabelOriginPosition_BottomRight:  X = 1.0f; Y = 1.0f; break;
        default:
            return Error_Construct2(ErrorCode_IllegalArgument, "LabelWidget_SetOriginPosition: position out of range.");
    }
    self->_origin = (Vector2){ .x = X, .y = Y };
    return Error_CreateSuccess();
}

float LabelWidget_GetRotation(const LabelWidget* self)
{
    return self->_rotation;
}

Error LabelWidget_SetRotation(LabelWidget* self, float rotationRad)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "LabelWidget_SetRotation: self must not be NULL.");
    }
    if (!IsFloatFinite(rotationRad))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "LabelWidget_SetRotation: rotation must be finite.");
    }
    self->_rotation = rotationRad;
    return Error_CreateSuccess();
}

float LabelWidget_GetSize(const LabelWidget* self)
{
    return self->_size;
}

Error LabelWidget_SetSize(LabelWidget* self, float size)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "LabelWidget_SetSize: self must not be NULL.");
    }
    if (!IsFloatFinite(size))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "LabelWidget_SetSize: size must be finite.");
    }
    self->_size = size;
    self->_isCacheValid = false; // wrap/resize depend on the size multiplier
    return Error_CreateSuccess();
}

RenderColor LabelWidget_GetTint(const LabelWidget* self)
{
    return self->_tint;
}

Error LabelWidget_SetTint(LabelWidget* self, RenderColor tint)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "LabelWidget_SetTint: self must not be NULL.");
    }
    if (!IsFloatFinite(tint.Brightness) || !IsFloatFinite(tint.Opacity))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "LabelWidget_SetTint: brightness and opacity must be finite.");
    }
    self->_tint = tint;
    return Error_CreateSuccess();
}

bool LabelWidget_HasBounds(const LabelWidget* self)
{
    return self->_hasBounds;
}

Vector2 LabelWidget_GetBounds(const LabelWidget* self)
{
    return self->_bounds;
}

Error LabelWidget_SetBounds(LabelWidget* self, Vector2 bounds)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "LabelWidget_SetBounds: self must not be NULL.");
    }
    if (!IsVector2Finite(bounds) || (bounds.x < 0.0f) || (bounds.y < 0.0f))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "LabelWidget_SetBounds: bounds must be finite and non-negative.");
    }
    self->_bounds = bounds;
    self->_hasBounds = true;
    self->_isCacheValid = false;
    return Error_CreateSuccess();
}

Error LabelWidget_ClearBounds(LabelWidget* self)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "LabelWidget_ClearBounds: self must not be NULL.");
    }
    self->_hasBounds = false;
    self->_isCacheValid = false;
    return Error_CreateSuccess();
}

LabelBoundHandling LabelWidget_GetBoundHandling(const LabelWidget* self)
{
    return self->_boundHandling;
}

Error LabelWidget_SetBoundHandling(LabelWidget* self, LabelBoundHandling mode)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "LabelWidget_SetBoundHandling: self must not be NULL.");
    }
    if ((mode != LabelBoundHandling_Cut) && (mode != LabelBoundHandling_Wrap) && (mode != LabelBoundHandling_Resize)
        && (mode != LabelBoundHandling_WrapThenResize) && (mode != LabelBoundHandling_WrapThenCut))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "LabelWidget_SetBoundHandling: mode out of range.");
    }
    self->_boundHandling = mode;
    self->_isCacheValid = false;
    return Error_CreateSuccess();
}

Error LabelWidget_GetDrawSize(LabelWidget* self, Vector2* outSize)
{
    if ((self == NULL) || (outSize == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "LabelWidget_GetDrawSize: self and outSize must not be NULL.");
    }

    Error CacheResult = EnsureCache(self);
    if (CacheResult.Code != ErrorCode_Success)
    {
        return CacheResult;
    }

    *outSize = (Vector2){ .x = self->_drawSizeFitted.x * self->_size, .y = self->_drawSizeFitted.y * self->_size };
    return Error_CreateSuccess();
}
