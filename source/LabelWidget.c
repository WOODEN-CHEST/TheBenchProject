#include "LabelWidget.h"
#include "UIWidget.h"
#include "UIScreen.h"
#include "UIWidgetFactory.h"
#include "UIRenderContext.h"
#include "TextComponent.h"
#include "TextComponentFactory.h"
#include "TextDocument.h"
#include "TextLayout.h"
#include "Renderer.h"
#include "raylib/raylib.h"
#include "wr/WRMemory.h"
#include "wr/WRMath.h"


// Static functions: small helpers.
static bool IsFloatFinite(float value)
{
    return !Math_IsNaNFloat(value) && !Math_IsInfinityFloat(value);
}

static bool IsVector2Finite(Vector2 value)
{
    return IsFloatFinite(value.x) && IsFloatFinite(value.y);
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

static TextLayoutAlignment ToLayoutAlignment(LabelAlignment alignment)
{
    switch (alignment)
    {
        case LabelAlignment_Left:
            return TextLayoutAlignment_Left;
        case LabelAlignment_Center:
            return TextLayoutAlignment_Center;
        case LabelAlignment_Right:
            return TextLayoutAlignment_Right;
    }
    return TextLayoutAlignment_Left;
}

// The layout the label's properties ask for; the setters keep every value valid for TextLayout_Build.
static TextLayoutOptions GetLayoutOptions(const LabelWidget* self)
{
    TextLayoutOptions Options = TextLayoutOptions_CreateDefault();
    Options.SizeMultiplier = self->_size;
    Options.HasMaxWidth = self->_hasBounds;
    Options.MaxWidth = self->_bounds.x;
    Options.IsWrapEnabled = ShouldWrap(self);
    // As before the text system's rebuild, zero-width bounds do not shrink the text to nothing.
    Options.IsShrinkEnabled = ShouldResize(self) && (self->_bounds.x > 0.0f);
    Options.Alignment = ToLayoutAlignment(self->_alignment);
    return Options;
}

static Error RefreshLayout(LabelWidget* self)
{
    TextLayoutOptions Options = GetLayoutOptions(self);
    return TextLayout_Refresh(&self->_layout, &self->_document, &Options);
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


// Static functions: the widget hooks.
/* Writes the label's widget box size to match the drawn text block (auto-fit), so hit-testing and the
 * keyboard-navigation outline wrap the text. Position (the anchor) is left untouched. */
// TODO: this writes widget state during render (AGENTS.md rule 3) because only the render context knows the
// target's aspect ratio. FIX_PLAN Phase 5 adds a Layout vtable slot for it.
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

static Error LabelWidget_UpdateVT(void* self, ProgramTime time)
{
    LabelWidget* Label = self;
    Error Result = TextDocument_Update(&Label->_document, time);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return RefreshLayout(Label);
}

static Error LabelWidget_RenderVT(void* self, UIRenderContext* context)
{
    LabelWidget* Label = self;
    RenderContext* Context = UIRenderContext_GetRenderContext(context);
    Vector2 Anchor = UIRenderContext_LocalToScreen(context, (Vector2){ .x = 0.0f, .y = 0.0f });
    Vector2 DrawSize = TextLayout_GetSize(&Label->_layout);

    // The block's top-left sits at the anchor; the origin (the rotation pivot) is placed accordingly.
    Vector2 OriginOffset = FittedToScreenRel(Context, (Vector2){ .x = Label->_origin.x * DrawSize.x,
        .y = Label->_origin.y * DrawSize.y });
    TextLayoutDrawArguments Arguments = TextLayoutDrawArguments_Create(RenderVector2D_Relative((Vector2)
    {
        .x = Anchor.x + OriginOffset.x,
        .y = Anchor.y + OriginOffset.y
    }));
    Arguments.RelativeOrigin = Label->_origin;
    Arguments.RotationRad = Label->_rotation;
    Arguments.Tint = UIRenderColor_Multiply(UIRenderContext_GetTint(context), Label->_tint);
    if (ShouldCut(Label))
    {
        Vector2 BoundsScreen = FittedToScreenRel(Context, Label->_bounds);
        Arguments.HasScissor = true;
        Arguments.ScissorRelative = (Rectangle){ .x = Anchor.x, .y = Anchor.y, .width = BoundsScreen.x,
            .height = BoundsScreen.y };
    }

    Error DrawResult = TextLayout_Draw(&Label->_layout, Context, &Arguments);
    if (DrawResult.Code != ErrorCode_Success)
    {
        return DrawResult;
    }
    return AutoFitBox(Label, Context, DrawSize);
}

static Error LabelWidget_OnDeconstructVT(void* self)
{
    LabelWidget* Label = self;
    Error First = TextLayout_Deconstruct(&Label->_layout);
    Error DocumentResult = TextDocument_Deconstruct(&Label->_document);
    if ((DocumentResult.Code != ErrorCode_Success) && (First.Code == ErrorCode_Success))
    {
        First = DocumentResult;
    }
    else
    {
        Error_Deconstruct(&DocumentResult);
    }
    return First;
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
    .Update = LabelWidget_UpdateVT,
    .Render = LabelWidget_RenderVT,
    .OnDeconstruct = LabelWidget_OnDeconstructVT,
    .GetProperty = LabelWidget_GetPropertyVT,
    .SetProperty = LabelWidget_SetPropertyVT
};


// Static functions: the widget constructor (registered with the factory).
// Everything that can fail happens before Widget_Construct, which registers the widget with its screen: a
// constructor failing after it would leave the screen pointing at a slot the factory frees.
static Error LabelWidget_ConstructVT(void* memory, UIScreen* screen, uint64_t typeId, void* args)
{
    LabelWidget* Label = memory;
    LabelWidgetArgs* Arguments = args;
    if ((Arguments == NULL) || (Arguments->ComponentFactory == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "LabelWidget: the ComponentFactory argument is required.");
    }

    Error Result = TextLayout_Construct(&Label->_layout);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = TextDocument_Construct(&Label->_document, Arguments->ComponentFactory, Arguments->Assets, Arguments->Logger);
    if (Result.Code != ErrorCode_Success)
    {
        Error Cleanup = TextLayout_Deconstruct(&Label->_layout);
        Error_Deconstruct(&Cleanup);
        return Result;
    }
    if (Arguments->Text != NULL)
    {
        Result = TextDocument_SetRoot(&Label->_document, Arguments->Text);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = Widget_Construct(&Label->Base, &LabelWidgetVTable, screen, typeId);
        if (Result.Code != ErrorCode_Success)
        {
            // The caller keeps the text when the label is not created.
            TextComponent* Returned = NULL;
            Error TakeResult = TextDocument_TakeRoot(&Label->_document, &Returned);
            Error_Deconstruct(&TakeResult);
        }
    }
    if (Result.Code != ErrorCode_Success)
    {
        Error Cleanup = TextDocument_Deconstruct(&Label->_document);
        Error_Deconstruct(&Cleanup);
        Cleanup = TextLayout_Deconstruct(&Label->_layout);
        Error_Deconstruct(&Cleanup);
        return Result;
    }

    Label->_alignment = LabelAlignment_Left;
    Label->_origin = (Vector2){ .x = 0.0f, .y = 0.0f };
    Label->_rotation = 0.0f;
    Label->_size = 1.0f;
    Label->_tint = RenderColor_White();
    Label->_hasBounds = false;
    Label->_bounds = (Vector2){ .x = 0.0f, .y = 0.0f };
    Label->_boundHandling = LabelBoundHandling_Cut;
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
    AssetManager* assets,
    Logger* logger,
    TextComponent* text,
    LabelWidget** outLabel)
{
    if ((screen == NULL) || (componentFactory == NULL) || (outLabel == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "LabelWidget_Create: screen, componentFactory and outLabel must not be NULL.");
    }
    *outLabel = NULL;

    LabelWidgetArgs Arguments = { .ComponentFactory = componentFactory, .Assets = assets, .Logger = logger, .Text = text };
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
    return TextDocument_GetRoot(&self->_document);
}

Error LabelWidget_SetText(LabelWidget* self, TextComponent* text)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "LabelWidget_SetText: self must not be NULL.");
    }
    return TextDocument_SetRoot(&self->_document, text);
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
    switch (alignment)
    {
        case LabelAlignment_Left:
        case LabelAlignment_Center:
        case LabelAlignment_Right:
            self->_alignment = alignment;
            return Error_CreateSuccess();
    }
    return Error_Construct2(ErrorCode_IllegalArgument, "LabelWidget_SetAlignment: alignment out of range.");
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

    switch (position)
    {
        case LabelOriginPosition_TopLeft:      return LabelWidget_SetOrigin(self, (Vector2){ .x = 0.0f, .y = 0.0f });
        case LabelOriginPosition_TopCenter:    return LabelWidget_SetOrigin(self, (Vector2){ .x = 0.5f, .y = 0.0f });
        case LabelOriginPosition_TopRight:     return LabelWidget_SetOrigin(self, (Vector2){ .x = 1.0f, .y = 0.0f });
        case LabelOriginPosition_MiddleLeft:   return LabelWidget_SetOrigin(self, (Vector2){ .x = 0.0f, .y = 0.5f });
        case LabelOriginPosition_MiddleCenter: return LabelWidget_SetOrigin(self, (Vector2){ .x = 0.5f, .y = 0.5f });
        case LabelOriginPosition_MiddleRight:  return LabelWidget_SetOrigin(self, (Vector2){ .x = 1.0f, .y = 0.5f });
        case LabelOriginPosition_BottomLeft:   return LabelWidget_SetOrigin(self, (Vector2){ .x = 0.0f, .y = 1.0f });
        case LabelOriginPosition_BottomCenter: return LabelWidget_SetOrigin(self, (Vector2){ .x = 0.5f, .y = 1.0f });
        case LabelOriginPosition_BottomRight:  return LabelWidget_SetOrigin(self, (Vector2){ .x = 1.0f, .y = 1.0f });
    }
    return Error_Construct2(ErrorCode_IllegalArgument, "LabelWidget_SetOriginPosition: position out of range.");
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
    if (!IsFloatFinite(size) || (size < 0.0f) || (size > TEXT_LAYOUT_SIZE_MULTIPLIER_MAX))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "LabelWidget_SetSize: size must be finite and within [0, TEXT_LAYOUT_SIZE_MULTIPLIER_MAX].");
    }
    self->_size = size;
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
    return Error_CreateSuccess();
}

Error LabelWidget_ClearBounds(LabelWidget* self)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "LabelWidget_ClearBounds: self must not be NULL.");
    }
    self->_hasBounds = false;
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
    switch (mode)
    {
        case LabelBoundHandling_Cut:
        case LabelBoundHandling_Wrap:
        case LabelBoundHandling_Resize:
        case LabelBoundHandling_WrapThenResize:
        case LabelBoundHandling_WrapThenCut:
            self->_boundHandling = mode;
            return Error_CreateSuccess();
    }
    return Error_Construct2(ErrorCode_IllegalArgument, "LabelWidget_SetBoundHandling: mode out of range.");
}

Error LabelWidget_GetDrawSize(LabelWidget* self, Vector2* outSize)
{
    if ((self == NULL) || (outSize == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "LabelWidget_GetDrawSize: self and outSize must not be NULL.");
    }

    Error Result = RefreshLayout(self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    *outSize = TextLayout_GetSize(&self->_layout);
    return Error_CreateSuccess();
}
