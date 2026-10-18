#include <inttypes.h>
#include <math.h>
#include "TextComponent.h"
#include "TextComponentInternal.h"
#include "AssetManager.h"
#include "wr/WRString.h"


// Static functions: validation.
static bool IsFloatInRange(float value, float minimum, float maximum)
{
    return isfinite((double)value) && (value >= minimum) && (value <= maximum);
}

static bool IsRenderColorValid(RenderColor color)
{
    return IsFloatInRange(color.Brightness, RENDER_COLOR_BRIGHTNESS_MIN, RENDER_COLOR_BRIGHTNESS_MAX)
        && IsFloatInRange(color.Opacity, RENDER_COLOR_OPACITY_MIN, RENDER_COLOR_OPACITY_MAX);
}

static bool IsSizeVectorValid(Vector2 size)
{
    return IsFloatInRange(size.x, 0.0f, TEXT_COMPONENT_SIZE_MAX) && IsFloatInRange(size.y, 0.0f, TEXT_COMPONENT_SIZE_MAX);
}

static bool IsShadowOffsetValid(Vector2 offset)
{
    return IsFloatInRange(offset.x, -STRING_COMPONENT_SHADOW_OFFSET_LIMIT, STRING_COMPONENT_SHADOW_OFFSET_LIMIT)
        && IsFloatInRange(offset.y, -STRING_COMPONENT_SHADOW_OFFSET_LIMIT, STRING_COMPONENT_SHADOW_OFFSET_LIMIT);
}


// Static functions: tree bookkeeping.
// Gives the component and every ancestor a new revision, so a changed root revision means "something below changed".
static void MarkChanged(TextComponent* self)
{
    uint64_t Revision = TextComponentFactory_NextRevision(self->_factory);
    for (TextComponent* Node = self; Node != NULL; Node = Node->_parent)
    {
        Node->_revision = Revision;
    }
}

static TextComponent* GetChild(const TextComponent* self, size_t index)
{
    if (self->_subComponents == NULL)
    {
        return NULL;
    }
    TextComponent** Slot = GenericBuffer_GetPointerToElement(self->_subComponents, index);
    return (Slot != NULL) ? *Slot : NULL;
}

// After a child of height childHeight was added below parent: heights only grow, so walk up while they do. O(depth).
static void GrowHeights(TextComponent* parent, uint32_t childHeight)
{
    uint32_t Levels = childHeight + 1U;
    for (TextComponent* Node = parent; (Node != NULL) && (Levels > Node->_height); Node = Node->_parent)
    {
        Node->_height = Levels;
        Levels++;
    }
}

// After node lost a child of height lostHeight (removed, or shrunk from that height): only a node that the child made
// that tall is rescanned, and the scan stops at the first child that still reaches the old height, so removing one of
// many equal children costs O(depth).
static void ShrinkHeights(TextComponent* node, uint32_t lostHeight)
{
    uint32_t LostLevels = lostHeight;
    for (TextComponent* Node = node; Node != NULL; Node = Node->_parent)
    {
        uint32_t Old = Node->_height;
        if ((LostLevels + 1U) < Old)
        {
            return;
        }
        uint32_t New = 1U;
        size_t ChildCount = TextComponent_GetSubComponentCount(Node);
        for (size_t Index = 0; (Index < ChildCount) && (New < Old); Index++)
        {
            uint32_t ChildLevels = GetChild(Node, Index)->_height + 1U;
            if (ChildLevels > New)
            {
                New = ChildLevels;
            }
        }
        if (New == Old)
        {
            return;
        }
        Node->_height = New;
        LostLevels = Old;
    }
}

static Error ValidateNewChild(const TextComponent* self, const TextComponent* child, const char* functionName)
{
    if (child->_factory != self->_factory)
    {
        return Error_Construct3(ErrorCode_IllegalArgument, u8"%s: the child was created by another factory.", functionName);
    }
    if (child->_parent != NULL)
    {
        return Error_Construct3(ErrorCode_InvalidOperation, u8"%s: the child already has a parent; detach it first.",
            functionName);
    }
    if (child->_isHeld)
    {
        return Error_Construct3(ErrorCode_InvalidOperation,
            u8"%s: the child is the tree of a text document; take it out of the document first (TextDocument_TakeRoot).",
            functionName);
    }
    if (TextComponent_IsAncestorOf(child, self))
    {
        return Error_Construct3(ErrorCode_InvalidOperation,
            u8"%s: a component cannot be added to itself or inside its own subtree.", functionName);
    }
    uint64_t Levels = (uint64_t)TextComponent_GetDepth(self) + (uint64_t)child->_height;
    if (Levels > (uint64_t)TEXT_COMPONENT_MAX_DEPTH)
    {
        return Error_Construct3(ErrorCode_InvalidOperation,
            u8"%s: the tree would have %" PRIu64 u8" levels; at most %d are allowed.", functionName, Levels,
            TEXT_COMPONENT_MAX_DEPTH);
    }
    return Error_CreateSuccess();
}

static Error InsertChild(TextComponent* self, size_t index, TextComponent* child, const char* functionName)
{
    Error Validation = ValidateNewChild(self, child, functionName);
    if (Validation.Code != ErrorCode_Success)
    {
        return Validation;
    }

    if (self->_subComponents == NULL)
    {
        Error BorrowResult = TextComponentFactory_BorrowBuffer(self->_factory, sizeof(TextComponent*), &self->_subComponents);
        if (BorrowResult.Code != ErrorCode_Success)
        {
            return BorrowResult;
        }
    }

    TextComponent* ChildPointer = child;
    if (!GenericBuffer_Insert(self->_subComponents, &ChildPointer, index))
    {
        return Error_Construct3(ErrorCode_BufferTooLarge, u8"%s: could not grow the subcomponent list.", functionName);
    }
    child->_parent = self;
    GrowHeights(self, child->_height);
    MarkChanged(self);
    return Error_CreateSuccess();
}

// Unlinks the child at index (which must exist) from self, leaving it a root.
static TextComponent* UnlinkChildAt(TextComponent* self, size_t index)
{
    TextComponent* Child = GetChild(self, index);
    GenericBuffer_RemoveAt(self->_subComponents, index);
    Child->_parent = NULL;
    ShrinkHeights(self, Child->_height);
    MarkChanged(self);
    return Child;
}


// Static functions: owned strings.
static bool IsWithinBuffer(const GenericBuffer* buffer, const unsigned char* pointer)
{
    if (buffer->_data == NULL)
    {
        return false;
    }
    uintptr_t Start = (uintptr_t)buffer->_data;
    uintptr_t Address = (uintptr_t)pointer;
    return (Address >= Start) && (Address < (Start + buffer->_capacity));
}

// Replaces the string held in *slot with a copy of value; NULL or "" releases the buffer. The copy is built in a fresh
// buffer when value points into the current one (a suffix of the old string), and capacity is reserved before the old
// string is cleared, so a failure always keeps the old value.
static Error AssignOwnedString(TextComponent* owner, GenericBuffer** slot, const unsigned char* value)
{
    GenericBuffer* Current = *slot;
    if ((value == NULL) || (value[0] == (unsigned char)0))
    {
        if (Current != NULL)
        {
            TextComponentFactory_ReturnBuffer(owner->_factory, Current);
            *slot = NULL;
        }
        return Error_CreateSuccess();
    }
    if ((Current != NULL) && (value == Current->_data))
    {
        return Error_CreateSuccess();
    }

    size_t RequiredBytes = 0;
    if (!Memory_TryAddSize(StringUTF8_GetByteLength(value), 1U, &RequiredBytes))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "TextComponent: the string is too long.");
    }

    GenericBuffer* Target = Current;
    if ((Target == NULL) || IsWithinBuffer(Current, value))
    {
        Error BorrowResult = TextComponentFactory_BorrowBuffer(owner->_factory, sizeof(unsigned char), &Target);
        if (BorrowResult.Code != ErrorCode_Success)
        {
            return BorrowResult;
        }
    }
    if (!GenericBuffer_EnsureTotalCapacity(Target, RequiredBytes))
    {
        if (Target != Current)
        {
            TextComponentFactory_ReturnBuffer(owner->_factory, Target);
        }
        return Error_Construct2(ErrorCode_BufferTooLarge, "TextComponent: could not grow a string buffer.");
    }

    // The capacity is reserved, so the clear-and-append below cannot fail.
    GenericBuffer_Clear(Target);
    GenericBuffer_AppendString(Target, value);
    GenericBuffer_NullTerminate(Target);
    if ((Current != NULL) && (Target != Current))
    {
        TextComponentFactory_ReturnBuffer(owner->_factory, Current);
    }
    *slot = Target;
    return Error_CreateSuccess();
}

static void ReleaseBorrowedBuffer(TextComponentFactory* factory, GenericBuffer** slot)
{
    if (*slot != NULL)
    {
        TextComponentFactory_ReturnBuffer(factory, *slot);
        *slot = NULL;
    }
}


// Static functions: per-type destruction and initialization.
// Releases the base's child-list buffer. The children themselves are returned by the factory's tree walk.
static void ReleaseBase(TextComponent* self)
{
    ReleaseBorrowedBuffer(self->_factory, &self->_subComponents);
    self->VTable = NULL;
    self->_parent = NULL;
}

static void ReleaseStringPart(StringComponent* self)
{
    ReleaseBorrowedBuffer(self->Base._factory, &self->_text);
    ReleaseBorrowedBuffer(self->Base._factory, &self->_fontName);
}

static void EmptyComponent_Destroy(void* self)
{
    EmptyComponent* Empty = self;
    ReleaseBase(&Empty->Base);
}

static void StringComponent_Destroy(void* self)
{
    StringComponent* String = self;
    ReleaseStringPart(String);
    ReleaseBase(&String->Base);
}

static void SpriteComponent_Destroy(void* self)
{
    SpriteComponent* Sprite = self;
    ReleaseBorrowedBuffer(Sprite->Base._factory, &Sprite->_animationName);
    ReleaseBase(&Sprite->Base);
}

static void TranslateComponent_Destroy(void* self)
{
    TranslateComponent* Translate = self;
    ReleaseBorrowedBuffer(Translate->Base.Base._factory, &Translate->_key);
    ReleaseStringPart(&Translate->Base);
    ReleaseBase(&Translate->Base.Base);
}

static const TextComponentVTable EmptyComponentVTable = { .Destroy = EmptyComponent_Destroy };
static const TextComponentVTable StringComponentVTable = { .Destroy = StringComponent_Destroy };
static const TextComponentVTable SpriteComponentVTable = { .Destroy = SpriteComponent_Destroy };
static const TextComponentVTable TranslateComponentVTable = { .Destroy = TranslateComponent_Destroy };

static void InitializeBase(TextComponent* self, const TextComponentVTable* vtable, TextComponentType type,
    TextComponentFactory* factory)
{
    self->VTable = vtable;
    self->_type = type;
    self->_factory = factory;
    self->_parent = NULL;
    self->_subComponents = NULL;
    self->_revision = TextComponentFactory_NextRevision(factory);
    self->_height = 1U;
    self->_isHeld = false;
}

static void InitializeStringPart(StringComponent* self)
{
    self->_text = NULL;
    self->_fontName = NULL;
    self->_color = RenderColor_White();
    self->_size = STRING_COMPONENT_DEFAULT_SIZE;
    self->_isShadowActive = STRING_COMPONENT_DEFAULT_IS_SHADOW_ACTIVE;
    self->_shadowColorType = TextShadowColorType_Default;
    self->_shadowColor = RenderColor_White();
    self->_shadowOffset = (Vector2){ .x = STRING_COMPONENT_DEFAULT_SHADOW_OFFSET_X, .y = STRING_COMPONENT_DEFAULT_SHADOW_OFFSET_Y };
    self->_isUnderlined = false;
    self->_isStrikethrough = false;
    self->_spacing = STRING_COMPONENT_DEFAULT_SPACING;
}


// Internal functions (TextComponentInternal.h).
void TextComponent_SetIsHeld(TextComponent* self, bool isHeld)
{
    self->_isHeld = isHeld;
}

void EmptyComponent_Initialize(EmptyComponent* self, TextComponentFactory* factory)
{
    InitializeBase(&self->Base, &EmptyComponentVTable, TextComponentType_Empty, factory);
}

void StringComponent_Initialize(StringComponent* self, TextComponentFactory* factory)
{
    InitializeBase(&self->Base, &StringComponentVTable, TextComponentType_String, factory);
    InitializeStringPart(self);
}

void SpriteComponent_Initialize(SpriteComponent* self, TextComponentFactory* factory)
{
    InitializeBase(&self->Base, &SpriteComponentVTable, TextComponentType_Sprite, factory);
    self->_animationName = NULL;
    self->_color = RenderColor_White();
    self->_size = (Vector2){ .x = 0.0f, .y = 0.0f };
}

void TranslateComponent_Initialize(TranslateComponent* self, TextComponentFactory* factory)
{
    InitializeBase(&self->Base.Base, &TranslateComponentVTable, TextComponentType_Translate, factory);
    InitializeStringPart(&self->Base);
    self->_key = NULL;
}


// Public functions: tree queries.
TextComponent* TextComponent_GetRoot(TextComponent* self)
{
    TextComponent* Node = self;
    while (Node->_parent != NULL)
    {
        Node = Node->_parent;
    }
    return Node;
}

size_t TextComponent_GetDepth(const TextComponent* self)
{
    size_t Depth = 1U;
    for (const TextComponent* Node = self->_parent; Node != NULL; Node = Node->_parent)
    {
        Depth++;
    }
    return Depth;
}

bool TextComponent_IsAncestorOf(const TextComponent* self, const TextComponent* other)
{
    if ((self == NULL) || (other == NULL))
    {
        return false;
    }
    for (const TextComponent* Node = other; Node != NULL; Node = Node->_parent)
    {
        if (Node == self)
        {
            return true;
        }
    }
    return false;
}


// Public functions: subcomponents.
Error TextComponent_AddSubComponent(TextComponent* self, TextComponent* child)
{
    if ((self == NULL) || (child == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "TextComponent_AddSubComponent: self and child must not be NULL.");
    }
    return InsertChild(self, TextComponent_GetSubComponentCount(self), child, "TextComponent_AddSubComponent");
}

Error TextComponent_InsertSubComponent(TextComponent* self, size_t index, TextComponent* child)
{
    if ((self == NULL) || (child == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextComponent_InsertSubComponent: self and child must not be NULL.");
    }
    if (index > TextComponent_GetSubComponentCount(self))
    {
        return Error_Construct2(ErrorCode_IndexOutOfBounds,
            "TextComponent_InsertSubComponent: index exceeds the subcomponent count.");
    }
    return InsertChild(self, index, child, "TextComponent_InsertSubComponent");
}

Error TextComponent_DetachSubComponentAt(TextComponent* self, size_t index, TextComponent** outChild)
{
    if ((self == NULL) || (outChild == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextComponent_DetachSubComponentAt: self and outChild must not be NULL.");
    }
    *outChild = NULL;
    if (index >= TextComponent_GetSubComponentCount(self))
    {
        return Error_Construct2(ErrorCode_IndexOutOfBounds,
            "TextComponent_DetachSubComponentAt: index is not less than the subcomponent count.");
    }

    *outChild = UnlinkChildAt(self, index);
    return Error_CreateSuccess();
}

Error TextComponent_Detach(TextComponent* self)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "TextComponent_Detach: self must not be NULL.");
    }
    TextComponent* Parent = self->_parent;
    if (Parent == NULL)
    {
        return Error_CreateSuccess();
    }

    size_t ChildCount = TextComponent_GetSubComponentCount(Parent);
    for (size_t Index = 0; Index < ChildCount; Index++)
    {
        if (GetChild(Parent, Index) == self)
        {
            UnlinkChildAt(Parent, Index);
            return Error_CreateSuccess();
        }
    }
    return Error_Construct2(ErrorCode_InvalidState, "TextComponent_Detach: the component is missing from its parent's list.");
}

Error TextComponent_DestroySubComponentAt(TextComponent* self, size_t index)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "TextComponent_DestroySubComponentAt: self must not be NULL.");
    }
    if (index >= TextComponent_GetSubComponentCount(self))
    {
        return Error_Construct2(ErrorCode_IndexOutOfBounds,
            "TextComponent_DestroySubComponentAt: index is not less than the subcomponent count.");
    }

    TextComponent* Child = UnlinkChildAt(self, index);
    return TextComponentFactory_ReturnSubtree(self->_factory, Child);
}

Error TextComponent_ClearSubComponents(TextComponent* self)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "TextComponent_ClearSubComponents: self must not be NULL.");
    }
    size_t ChildCount = TextComponent_GetSubComponentCount(self);
    if (ChildCount == 0U)
    {
        return Error_CreateSuccess();
    }

    Error First = Error_CreateSuccess();
    for (size_t Index = ChildCount; Index > 0U; Index--)
    {
        TextComponent* Child = GetChild(self, Index - 1U);
        Child->_parent = NULL;
        Error ReturnResult = TextComponentFactory_ReturnSubtree(self->_factory, Child);
        if ((ReturnResult.Code != ErrorCode_Success) && (First.Code == ErrorCode_Success))
        {
            First = ReturnResult;
        }
        else
        {
            Error_Deconstruct(&ReturnResult);
        }
    }
    GenericBuffer_Clear(self->_subComponents);
    uint32_t OldHeight = self->_height;
    self->_height = 1U;
    if (self->_parent != NULL)
    {
        ShrinkHeights(self->_parent, OldHeight);
    }
    MarkChanged(self);
    return First;
}

size_t TextComponent_GetSubComponentCount(const TextComponent* self)
{
    if ((self == NULL) || (self->_subComponents == NULL))
    {
        return 0U;
    }
    return self->_subComponents->_count;
}

TextComponent* TextComponent_GetSubComponentAt(const TextComponent* self, size_t index)
{
    if (self == NULL)
    {
        return NULL;
    }
    return GetChild(self, index);
}


// Public functions: StringComponent properties.
Error StringComponent_SetText(StringComponent* self, const unsigned char* text)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "StringComponent_SetText: self must not be NULL.");
    }
    if ((text != NULL) && !StringUTF8_IsEncodingValid(text))
    {
        return Error_Construct2(ErrorCode_InvalidTextEncoding, "StringComponent_SetText: text must be valid UTF-8.");
    }

    Error Result = AssignOwnedString(&self->Base, &self->_text, text);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    MarkChanged(&self->Base);
    return Error_CreateSuccess();
}

Error StringComponent_SetFontName(StringComponent* self, const unsigned char* fontName)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "StringComponent_SetFontName: self must not be NULL.");
    }
    if ((fontName != NULL) && !AssetManager_IsValidAssetName(fontName))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "StringComponent_SetFontName: the font name must be an asset name ([a-z][a-z0-9_.-]*).");
    }

    Error Result = AssignOwnedString(&self->Base, &self->_fontName, fontName);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    MarkChanged(&self->Base);
    return Error_CreateSuccess();
}

Error StringComponent_SetColor(StringComponent* self, RenderColor color)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "StringComponent_SetColor: self must not be NULL.");
    }
    if (!IsRenderColorValid(color))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "StringComponent_SetColor: brightness and opacity must be finite and within [0, 1].");
    }

    self->_color = color;
    MarkChanged(&self->Base);
    return Error_CreateSuccess();
}

Error StringComponent_SetSize(StringComponent* self, float size)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "StringComponent_SetSize: self must not be NULL.");
    }
    if (!IsFloatInRange(size, 0.0f, TEXT_COMPONENT_SIZE_MAX))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "StringComponent_SetSize: size must be finite and within [0, TEXT_COMPONENT_SIZE_MAX].");
    }

    self->_size = size;
    MarkChanged(&self->Base);
    return Error_CreateSuccess();
}

Error StringComponent_SetIsShadowActive(StringComponent* self, bool isShadowActive)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "StringComponent_SetIsShadowActive: self must not be NULL.");
    }

    self->_isShadowActive = isShadowActive;
    MarkChanged(&self->Base);
    return Error_CreateSuccess();
}

Error StringComponent_SetShadowColorCustom(StringComponent* self, RenderColor color)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "StringComponent_SetShadowColorCustom: self must not be NULL.");
    }
    if (!IsRenderColorValid(color))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "StringComponent_SetShadowColorCustom: brightness and opacity must be finite and within [0, 1].");
    }

    self->_shadowColor = color;
    self->_shadowColorType = TextShadowColorType_Custom;
    MarkChanged(&self->Base);
    return Error_CreateSuccess();
}

Error StringComponent_SetShadowColorDefault(StringComponent* self)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "StringComponent_SetShadowColorDefault: self must not be NULL.");
    }

    self->_shadowColorType = TextShadowColorType_Default;
    MarkChanged(&self->Base);
    return Error_CreateSuccess();
}

Error StringComponent_SetShadowOffset(StringComponent* self, Vector2 offset)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "StringComponent_SetShadowOffset: self must not be NULL.");
    }
    if (!IsShadowOffsetValid(offset))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "StringComponent_SetShadowOffset: each offset component must be finite and within "
            "+-STRING_COMPONENT_SHADOW_OFFSET_LIMIT.");
    }

    self->_shadowOffset = offset;
    MarkChanged(&self->Base);
    return Error_CreateSuccess();
}

Error StringComponent_SetIsUnderlined(StringComponent* self, bool isUnderlined)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "StringComponent_SetIsUnderlined: self must not be NULL.");
    }

    self->_isUnderlined = isUnderlined;
    MarkChanged(&self->Base);
    return Error_CreateSuccess();
}

Error StringComponent_SetIsStrikethrough(StringComponent* self, bool isStrikethrough)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "StringComponent_SetIsStrikethrough: self must not be NULL.");
    }

    self->_isStrikethrough = isStrikethrough;
    MarkChanged(&self->Base);
    return Error_CreateSuccess();
}

Error StringComponent_SetSpacing(StringComponent* self, float spacing)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "StringComponent_SetSpacing: self must not be NULL.");
    }
    if (!IsFloatInRange(spacing, STRING_COMPONENT_SPACING_MIN, STRING_COMPONENT_SPACING_MAX))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "StringComponent_SetSpacing: spacing must be finite and within "
            "[STRING_COMPONENT_SPACING_MIN, STRING_COMPONENT_SPACING_MAX].");
    }

    self->_spacing = spacing;
    MarkChanged(&self->Base);
    return Error_CreateSuccess();
}


// Public functions: SpriteComponent properties.
Error SpriteComponent_SetAnimationName(SpriteComponent* self, const unsigned char* animationName)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "SpriteComponent_SetAnimationName: self must not be NULL.");
    }
    if ((animationName != NULL) && !AssetManager_IsValidAssetName(animationName))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "SpriteComponent_SetAnimationName: the animation name must be an asset name ([a-z][a-z0-9_.-]*).");
    }

    Error Result = AssignOwnedString(&self->Base, &self->_animationName, animationName);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    MarkChanged(&self->Base);
    return Error_CreateSuccess();
}

Error SpriteComponent_SetColor(SpriteComponent* self, RenderColor color)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "SpriteComponent_SetColor: self must not be NULL.");
    }
    if (!IsRenderColorValid(color))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "SpriteComponent_SetColor: brightness and opacity must be finite and within [0, 1].");
    }

    self->_color = color;
    MarkChanged(&self->Base);
    return Error_CreateSuccess();
}

Error SpriteComponent_SetSize(SpriteComponent* self, Vector2 size)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "SpriteComponent_SetSize: self must not be NULL.");
    }
    if (!IsSizeVectorValid(size))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "SpriteComponent_SetSize: each size component must be finite and within [0, TEXT_COMPONENT_SIZE_MAX].");
    }

    self->_size = size;
    MarkChanged(&self->Base);
    return Error_CreateSuccess();
}


// Public functions: TranslateComponent properties.
Error TranslateComponent_SetKey(TranslateComponent* self, const unsigned char* key)
{
    if ((self == NULL) || (key == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "TranslateComponent_SetKey: self and key must not be NULL.");
    }
    if (!TextComponent_IsValidTranslationKey(key))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "TranslateComponent_SetKey: the key must match [a-z][a-z0-9_.-]*.");
    }

    Error Result = AssignOwnedString(&self->Base.Base, &self->_key, key);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    MarkChanged(&self->Base.Base);
    return Error_CreateSuccess();
}

Error TranslateComponent_SetFallback(TranslateComponent* self, const unsigned char* fallback)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "TranslateComponent_SetFallback: self must not be NULL.");
    }
    return StringComponent_SetText(&self->Base, fallback);
}


// Public functions: validation.
bool TextComponent_IsValidTranslationKey(const unsigned char* key)
{
    // Keys follow the asset-name grammar on purpose: both will be namespaced per story pack the same way.
    return AssetManager_IsValidAssetName(key);
}
