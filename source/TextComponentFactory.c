#include "TextComponentFactory.h"
#include "TextComponentInternal.h"
#include "wr/WRChar.h"
#include "wr/WRMemory.h"


// Macros.
// Growth granularity of the component pools; components are small, so a modest section keeps allocations rare
// without wasting much memory.
#define TEXT_COMPONENT_POOL_SECTION_CAPACITY ((size_t)32)
// Number of component pools (one per TextComponentType).
#define TEXT_COMPONENT_POOL_COUNT ((size_t)4)


// Static functions.
// Keeps the first failure in *first and releases every later error so none leak.
static void KeepFirstError(Error* first, Error incoming)
{
    if ((incoming.Code != ErrorCode_Success) && (first->Code == ErrorCode_Success))
    {
        *first = incoming;
        return;
    }
    Error_Deconstruct(&incoming);
}

static ObjectPool* GetPoolForType(TextComponentFactory* self, TextComponentType type)
{
    switch (type)
    {
        case TextComponentType_Empty:
            return &self->_emptyPool;
        case TextComponentType_String:
            return &self->_stringPool;
        case TextComponentType_Sprite:
            return &self->_spritePool;
        case TextComponentType_Translate:
            return &self->_translatePool;
    }
    return NULL;
}

// Borrows a zero-filled slot from the type's pool and counts it as a live component.
static Error BorrowSlot(TextComponentFactory* self, TextComponentType type, void** outSlot)
{
    *outSlot = NULL;
    ObjectPool* Pool = GetPoolForType(self, type);
    if (Pool == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidState, "TextComponentFactory: unknown component type.");
    }

    Error Result = ObjectPool_GetNewObject(Pool, outSlot);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    self->_liveComponentCount++;
    return Error_CreateSuccess();
}

// Releases what one component owns and gives its slot back to its pool (its children are not touched).
static Error DisposeComponent(TextComponentFactory* self, TextComponent* component)
{
    ObjectPool* Pool = GetPoolForType(self, component->_type);
    if (Pool == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidState, "TextComponentFactory: unknown component type.");
    }

    component->VTable->Destroy(component);
    self->_liveComponentCount--;
    return ObjectPool_DisposeObject(Pool, component);
}

// Wraps the formatted scratch text in a new string component (the text is copied, so the scratch can be reused).
static Error CreateStringFromScratch(TextComponentFactory* self, StringComponent** outComponent)
{
    if (!GenericBuffer_NullTerminate(&self->_formatScratch))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "TextComponentFactory: could not terminate a formatted number.");
    }
    return TextComponentFactory_CreateString(self, self->_formatScratch._data, outComponent);
}

static Error CopyStringStyle(StringComponent* target, const StringComponent* source)
{
    Error Result = StringComponent_SetFontName(target, StringComponent_GetFontName(source));
    if (Result.Code == ErrorCode_Success)
    {
        Result = StringComponent_SetColor(target, StringComponent_GetColor(source));
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = StringComponent_SetSize(target, StringComponent_GetSize(source));
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = StringComponent_SetIsShadowActive(target, StringComponent_IsShadowActive(source));
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = (StringComponent_GetShadowColorType(source) == TextShadowColorType_Custom)
            ? StringComponent_SetShadowColorCustom(target, StringComponent_GetShadowColor(source))
            : StringComponent_SetShadowColorDefault(target);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = (StringComponent_GetShadowOffsetType(source) == TextShadowOffsetType_Custom)
            ? StringComponent_SetShadowOffsetCustom(target, StringComponent_GetShadowOffset(source))
            : StringComponent_SetShadowOffsetDefault(target);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = StringComponent_SetIsUnderlined(target, StringComponent_IsUnderlined(source));
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = StringComponent_SetIsStrikethrough(target, StringComponent_IsStrikethrough(source));
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = StringComponent_SetSpacing(target, StringComponent_GetSpacing(source));
    }
    return Result;
}

// Creates a childless copy of one component: its type, strings and properties.
static Error CloneOwnContent(TextComponentFactory* self, const TextComponent* source, TextComponent** outClone)
{
    *outClone = NULL;
    Error Result = Error_CreateSuccess();
    TextComponent* Clone = NULL;

    switch (TextComponent_GetType(source))
    {
        case TextComponentType_Empty:
        {
            EmptyComponent* Empty = NULL;
            Result = TextComponentFactory_CreateEmpty(self, &Empty);
            Clone = (Empty != NULL) ? EmptyComponent_AsComponent(Empty) : NULL;
            break;
        }
        case TextComponentType_String:
        {
            const StringComponent* Source = TextComponent_AsConstStringComponent(source);
            StringComponent* String = NULL;
            Result = TextComponentFactory_CreateString(self, StringComponent_GetText(Source), &String);
            Clone = (String != NULL) ? StringComponent_AsComponent(String) : NULL;
            if (Result.Code == ErrorCode_Success)
            {
                Result = CopyStringStyle(String, Source);
            }
            break;
        }
        case TextComponentType_Sprite:
        {
            const SpriteComponent* Source = TextComponent_AsConstSpriteComponent(source);
            SpriteComponent* Sprite = NULL;
            Result = TextComponentFactory_CreateSprite(self, SpriteComponent_GetAnimationName(Source), &Sprite);
            Clone = (Sprite != NULL) ? SpriteComponent_AsComponent(Sprite) : NULL;
            if (Result.Code == ErrorCode_Success)
            {
                Result = SpriteComponent_SetColor(Sprite, SpriteComponent_GetColor(Source));
            }
            if (Result.Code == ErrorCode_Success)
            {
                Result = SpriteComponent_SetSize(Sprite, SpriteComponent_GetSize(Source));
            }
            break;
        }
        case TextComponentType_Translate:
        {
            const TranslateComponent* Source = TextComponent_AsConstTranslateComponent(source);
            TranslateComponent* Translate = NULL;
            Result = TextComponentFactory_CreateTranslate(self, TranslateComponent_GetKey(Source),
                TranslateComponent_GetFallback(Source), &Translate);
            Clone = (Translate != NULL) ? TranslateComponent_AsComponent(Translate) : NULL;
            if (Result.Code == ErrorCode_Success)
            {
                Result = CopyStringStyle(TranslateComponent_AsString(Translate), &Source->Base);
            }
            break;
        }
    }

    if ((Result.Code == ErrorCode_Success) && (Clone == NULL))
    {
        Result = Error_Construct2(ErrorCode_InvalidState, "TextComponentFactory_CloneTree: unknown component type.");
    }
    if (Result.Code != ErrorCode_Success)
    {
        if (Clone != NULL)
        {
            Error DisposeResult = DisposeComponent(self, Clone);
            Error_Deconstruct(&DisposeResult);
        }
        return Result;
    }

    *outClone = Clone;
    return Error_CreateSuccess();
}

// Recursion depth is bounded by the source tree's depth limit.
static Error CloneNode(TextComponentFactory* self, const TextComponent* source, TextComponent** outClone)
{
    TextComponent* Clone = NULL;
    Error Result = CloneOwnContent(self, source, &Clone);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    size_t ChildCount = TextComponent_GetSubComponentCount(source);
    for (size_t Index = 0; Index < ChildCount; Index++)
    {
        TextComponent* ChildClone = NULL;
        Result = CloneNode(self, TextComponent_GetSubComponentAt(source, Index), &ChildClone);
        if (Result.Code == ErrorCode_Success)
        {
            Result = TextComponent_AddSubComponent(Clone, ChildClone);
            if (Result.Code != ErrorCode_Success)
            {
                Error ChildCleanup = TextComponentFactory_ReturnSubtree(self, ChildClone);
                Error_Deconstruct(&ChildCleanup);
            }
        }
        if (Result.Code != ErrorCode_Success)
        {
            Error Cleanup = TextComponentFactory_ReturnSubtree(self, Clone);
            Error_Deconstruct(&Cleanup);
            return Result;
        }
    }

    *outClone = Clone;
    return Error_CreateSuccess();
}


// Internal functions (TextComponentInternal.h).
Error TextComponentFactory_BorrowBuffer(TextComponentFactory* self, size_t elementSize, GenericBuffer** outBuffer)
{
    return BufferPool_Borrow(&self->_buffers, elementSize, outBuffer);
}

void TextComponentFactory_ReturnBuffer(TextComponentFactory* self, GenericBuffer* buffer)
{
    // Returning only fails for a buffer that did not come from this pool, which the components never hold; the pool
    // releases everything at teardown in any case.
    Error Result = BufferPool_Return(&self->_buffers, buffer);
    Error_Deconstruct(&Result);
}

uint64_t TextComponentFactory_NextRevision(TextComponentFactory* self)
{
    self->_revisionCounter++;
    return self->_revisionCounter;
}

Error TextComponentFactory_ReturnSubtree(TextComponentFactory* self, TextComponent* root)
{
    Error First = Error_CreateSuccess();

    // Children first; the walk is bounded by the tree's depth limit. root's list stays intact until its own Destroy.
    size_t ChildCount = TextComponent_GetSubComponentCount(root);
    for (size_t Index = 0; Index < ChildCount; Index++)
    {
        KeepFirstError(&First, TextComponentFactory_ReturnSubtree(self, TextComponent_GetSubComponentAt(root, Index)));
    }
    KeepFirstError(&First, DisposeComponent(self, root));
    return First;
}


// Public functions: lifecycle.
Error TextComponentFactory_Construct(TextComponentFactory* self)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "TextComponentFactory_Construct: self must not be NULL.");
    }

    ObjectPool* Pools[TEXT_COMPONENT_POOL_COUNT] = { &self->_emptyPool, &self->_stringPool, &self->_spritePool,
        &self->_translatePool };
    size_t ElementSizes[TEXT_COMPONENT_POOL_COUNT] = { sizeof(EmptyComponent), sizeof(StringComponent),
        sizeof(SpriteComponent), sizeof(TranslateComponent) };

    Error Result = Error_CreateSuccess();
    size_t ConstructedPools = 0;
    for (; ConstructedPools < TEXT_COMPONENT_POOL_COUNT; ConstructedPools++)
    {
        Result = ObjectPool_Construct1(Pools[ConstructedPools], ElementSizes[ConstructedPools],
            TEXT_COMPONENT_POOL_SECTION_CAPACITY);
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = BufferPool_Construct1(&self->_buffers);
    }
    if (Result.Code != ErrorCode_Success)
    {
        for (size_t Index = 0; Index < ConstructedPools; Index++)
        {
            Error Cleanup = ObjectPool_Deconstruct(Pools[Index]);
            Error_Deconstruct(&Cleanup);
        }
        return Result;
    }

    GenericBuffer_AllocateVariable(&self->_formatScratch, 0U, sizeof(unsigned char));
    self->_revisionCounter = 0U;
    self->_liveComponentCount = 0U;
    return Error_CreateSuccess();
}

Error TextComponentFactory_Deconstruct(TextComponentFactory* self)
{
    if (self == NULL)
    {
        return Error_CreateSuccess();
    }

    Error First = Error_CreateSuccess();
    KeepFirstError(&First, ObjectPool_Deconstruct(&self->_emptyPool));
    KeepFirstError(&First, ObjectPool_Deconstruct(&self->_stringPool));
    KeepFirstError(&First, ObjectPool_Deconstruct(&self->_spritePool));
    KeepFirstError(&First, ObjectPool_Deconstruct(&self->_translatePool));
    KeepFirstError(&First, BufferPool_Deconstruct(&self->_buffers));
    Memory_Free(self->_formatScratch._data);
    self->_formatScratch._data = NULL;
    self->_liveComponentCount = 0U;
    return First;
}


// Public functions: creation.
Error TextComponentFactory_CreateEmpty(TextComponentFactory* self, EmptyComponent** outComponent)
{
    if ((self == NULL) || (outComponent == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextComponentFactory_CreateEmpty: self and outComponent must not be NULL.");
    }
    *outComponent = NULL;

    void* Slot = NULL;
    Error Result = BorrowSlot(self, TextComponentType_Empty, &Slot);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    EmptyComponent_Initialize(Slot, self);
    *outComponent = Slot;
    return Error_CreateSuccess();
}

Error TextComponentFactory_CreateString(TextComponentFactory* self, const unsigned char* text, StringComponent** outComponent)
{
    if ((self == NULL) || (outComponent == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextComponentFactory_CreateString: self and outComponent must not be NULL.");
    }
    *outComponent = NULL;

    void* Slot = NULL;
    Error Result = BorrowSlot(self, TextComponentType_String, &Slot);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    StringComponent* Component = Slot;
    StringComponent_Initialize(Component, self);

    Result = StringComponent_SetText(Component, text);
    if (Result.Code != ErrorCode_Success)
    {
        Error Cleanup = DisposeComponent(self, StringComponent_AsComponent(Component));
        Error_Deconstruct(&Cleanup);
        return Result;
    }
    *outComponent = Component;
    return Error_CreateSuccess();
}

Error TextComponentFactory_CreateSprite(TextComponentFactory* self, const unsigned char* animationName,
    SpriteComponent** outComponent)
{
    if ((self == NULL) || (outComponent == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextComponentFactory_CreateSprite: self and outComponent must not be NULL.");
    }
    *outComponent = NULL;

    void* Slot = NULL;
    Error Result = BorrowSlot(self, TextComponentType_Sprite, &Slot);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    SpriteComponent* Component = Slot;
    SpriteComponent_Initialize(Component, self);

    Result = SpriteComponent_SetAnimationName(Component, animationName);
    if (Result.Code != ErrorCode_Success)
    {
        Error Cleanup = DisposeComponent(self, SpriteComponent_AsComponent(Component));
        Error_Deconstruct(&Cleanup);
        return Result;
    }
    *outComponent = Component;
    return Error_CreateSuccess();
}

Error TextComponentFactory_CreateTranslate(TextComponentFactory* self, const unsigned char* key,
    const unsigned char* fallback, TranslateComponent** outComponent)
{
    if ((self == NULL) || (key == NULL) || (outComponent == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextComponentFactory_CreateTranslate: self, key and outComponent must not be NULL.");
    }
    *outComponent = NULL;

    void* Slot = NULL;
    Error Result = BorrowSlot(self, TextComponentType_Translate, &Slot);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    TranslateComponent* Component = Slot;
    TranslateComponent_Initialize(Component, self);

    Result = TranslateComponent_SetKey(Component, key);
    if (Result.Code == ErrorCode_Success)
    {
        Result = TranslateComponent_SetFallback(Component, fallback);
    }
    if (Result.Code != ErrorCode_Success)
    {
        Error Cleanup = DisposeComponent(self, TranslateComponent_AsComponent(Component));
        Error_Deconstruct(&Cleanup);
        return Result;
    }
    *outComponent = Component;
    return Error_CreateSuccess();
}


// Public functions: shorthands.
Error TextComponentFactory_CreateSpace(TextComponentFactory* self, StringComponent** outComponent)
{
    return TextComponentFactory_CreateString(self, (const unsigned char*)u8" ", outComponent);
}

Error TextComponentFactory_CreateTab(TextComponentFactory* self, StringComponent** outComponent)
{
    return TextComponentFactory_CreateString(self, (const unsigned char*)u8"\t", outComponent);
}

Error TextComponentFactory_CreateNewline(TextComponentFactory* self, StringComponent** outComponent)
{
    return TextComponentFactory_CreateString(self, (const unsigned char*)u8"\n", outComponent);
}

Error TextComponentFactory_CreateInt64(TextComponentFactory* self, int64_t value, StringComponent** outComponent)
{
    return TextComponentFactory_CreateInt64Formatted(self, value, NUMBER_BASE_10, false, outComponent);
}

Error TextComponentFactory_CreateInt64Formatted(TextComponentFactory* self, int64_t value, int32_t base,
    bool includePrefix, StringComponent** outComponent)
{
    if ((self == NULL) || (outComponent == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextComponentFactory_CreateInt64Formatted: self and outComponent must not be NULL.");
    }
    *outComponent = NULL;

    GenericBuffer_Clear(&self->_formatScratch);
    Error FormatResult = Number_Int64ToString(value, base, includePrefix, &self->_formatScratch);
    if (FormatResult.Code != ErrorCode_Success)
    {
        return FormatResult;
    }
    return CreateStringFromScratch(self, outComponent);
}

Error TextComponentFactory_CreateUInt64(TextComponentFactory* self, uint64_t value, StringComponent** outComponent)
{
    return TextComponentFactory_CreateUInt64Formatted(self, value, NUMBER_BASE_10, false, outComponent);
}

Error TextComponentFactory_CreateUInt64Formatted(TextComponentFactory* self, uint64_t value, int32_t base,
    bool includePrefix, StringComponent** outComponent)
{
    if ((self == NULL) || (outComponent == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextComponentFactory_CreateUInt64Formatted: self and outComponent must not be NULL.");
    }
    *outComponent = NULL;

    GenericBuffer_Clear(&self->_formatScratch);
    Error FormatResult = Number_UInt64ToString(value, base, includePrefix, &self->_formatScratch);
    if (FormatResult.Code != ErrorCode_Success)
    {
        return FormatResult;
    }
    return CreateStringFromScratch(self, outComponent);
}

Error TextComponentFactory_CreateDouble(TextComponentFactory* self, double value, StringComponent** outComponent)
{
    return TextComponentFactory_CreateDoubleFormatted(self, value, DecimalFormatOptions_CreateShortest(DecimalSeparator_Period),
        outComponent);
}

Error TextComponentFactory_CreateDoubleFormatted(TextComponentFactory* self, double value, DecimalFormatOptions options,
    StringComponent** outComponent)
{
    if ((self == NULL) || (outComponent == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextComponentFactory_CreateDoubleFormatted: self and outComponent must not be NULL.");
    }
    *outComponent = NULL;

    GenericBuffer_Clear(&self->_formatScratch);
    Error FormatResult = Number_DoubleToString(value, &self->_formatScratch, options);
    if (FormatResult.Code != ErrorCode_Success)
    {
        return FormatResult;
    }
    return CreateStringFromScratch(self, outComponent);
}

Error TextComponentFactory_CreateCodepoint(TextComponentFactory* self, CodePoint codepoint, StringComponent** outComponent)
{
    if ((self == NULL) || (outComponent == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextComponentFactory_CreateCodepoint: self and outComponent must not be NULL.");
    }
    *outComponent = NULL;
    if (!CharUTF8_IsCodePointValid(codepoint) || (codepoint == (CodePoint)0))
    {
        return Error_Construct2(ErrorCode_InvalidCodePoint,
            "TextComponentFactory_CreateCodepoint: codepoint must be a Unicode scalar value other than U+0000.");
    }

    unsigned char Encoded[CODEPOINT_BYTE_COUNT_MAX + 1] = { 0 };
    CharUTF8_WriteCodePoint(Encoded, codepoint);
    return TextComponentFactory_CreateString(self, Encoded, outComponent);
}


// Public functions: clone and return.
Error TextComponentFactory_CloneTree(TextComponentFactory* self, const TextComponent* source, TextComponent** outRoot)
{
    if ((self == NULL) || (source == NULL) || (outRoot == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextComponentFactory_CloneTree: self, source and outRoot must not be NULL.");
    }
    *outRoot = NULL;
    return CloneNode(self, source, outRoot);
}

Error TextComponentFactory_ReturnTree(TextComponentFactory* self, TextComponent* root)
{
    if ((self == NULL) || (root == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "TextComponentFactory_ReturnTree: self and root must not be NULL.");
    }
    if (root->_factory != self)
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextComponentFactory_ReturnTree: the component was created by another factory.");
    }
    if (root->_parent != NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation,
            "TextComponentFactory_ReturnTree: the component still has a parent; detach it or destroy it through the "
            "parent.");
    }
    if (root->_isHeld)
    {
        return Error_Construct2(ErrorCode_InvalidOperation,
            "TextComponentFactory_ReturnTree: the component is the tree of a text document; the document returns it.");
    }
    return TextComponentFactory_ReturnSubtree(self, root);
}
