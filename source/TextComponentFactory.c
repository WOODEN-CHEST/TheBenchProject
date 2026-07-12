#include "TextComponentFactory.h"
#include "wr/WRChar.h"
#include "wr/WRMemory.h"


// Macros.
// Growth granularity for the component pools; components are small so a modest section keeps
// allocations rare without wasting much memory.
#define TEXT_COMPONENT_POOL_SECTION_CAPACITY ((size_t)32)


// Static functions.
static ObjectPool* GetPoolForType(TextComponentFactory* self, TextComponentType type)
{
    switch (type)
    {
        case TextComponentType_String:
            return &self->_stringPool;
        case TextComponentType_Sprite:
            return &self->_spritePool;
        case TextComponentType_Empty:
            return &self->_emptyPool;
        default:
            return NULL;
    }
}

static size_t StructSizeForType(TextComponentType type)
{
    switch (type)
    {
        case TextComponentType_String:
            return sizeof(StringComponent);
        case TextComponentType_Sprite:
            return sizeof(SpriteComponent);
        case TextComponentType_Empty:
            return sizeof(EmptyComponent);
        default:
            return 0;
    }
}

// Keeps the first failure in *first and releases every later error so none leak.
static void AccumulateError(Error* first, Error incoming)
{
    if (incoming.Code == ErrorCode_Success)
    {
        Error_Deconstruct(&incoming);
        return;
    }
    if (first->Code == ErrorCode_Success)
    {
        *first = incoming;
    }
    else
    {
        Error_Deconstruct(&incoming);
    }
}

// Borrows a pool slot for the type plus a subcomponent buffer, rolling back the slot if the buffer fails.
static Error BorrowStorage(TextComponentFactory* self, TextComponentType type, void** outSlot, GenericBuffer** outBuffer)
{
    *outSlot = NULL;
    *outBuffer = NULL;

    ObjectPool* Pool = GetPoolForType(self, type);
    if (Pool == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "BorrowStorage: unknown component type.");
    }

    void* Slot = NULL;
    Error SlotResult = ObjectPool_GetNewObject(Pool, &Slot);
    if (SlotResult.Code != ErrorCode_Success)
    {
        return SlotResult;
    }

    GenericBuffer* Buffer = NULL;
    Error BufferResult = BufferPool_Borrow(&self->_subComponentBuffers, sizeof(TextComponent*), &Buffer);
    if (BufferResult.Code != ErrorCode_Success)
    {
        Error DisposeResult = ObjectPool_DisposeObject(Pool, Slot);
        Error_Deconstruct(&DisposeResult);
        return BufferResult;
    }

    *outSlot = Slot;
    *outBuffer = Buffer;
    return Error_CreateSuccess();
}

// Returns freshly-borrowed storage after a construction failure (best-effort, errors released).
static void ReturnStorage(TextComponentFactory* self, TextComponentType type, void* slot, GenericBuffer* buffer)
{
    Error BufferResult = BufferPool_Return(&self->_subComponentBuffers, buffer);
    Error_Deconstruct(&BufferResult);

    ObjectPool* Pool = GetPoolForType(self, type);
    if (Pool != NULL)
    {
        Error DisposeResult = ObjectPool_DisposeObject(Pool, slot);
        Error_Deconstruct(&DisposeResult);
    }
}

// Applies font/color/size to a freshly-created string component; returns the first setter failure.
static Error ApplyStringStyle(StringComponent* component, GameFont font, RenderColor color, float size)
{
    Error Result = StringComponent_SetFont(component, font);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = StringComponent_SetColor(component, color);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return StringComponent_SetSize(component, size);
}

// Validates that a destination buffer is a usable byte buffer for generated text.
static Error ValidateDestinationBuffer(const GenericBuffer* destinationString)
{
    if (destinationString == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "TextComponentFactory: destinationString must not be NULL.");
    }
    if (destinationString->_elementSize != sizeof(unsigned char))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextComponentFactory: destinationString must be a byte buffer (element size 1).");
    }
    return Error_CreateSuccess();
}


// Public functions.
Error TextComponentFactory_Construct(TextComponentFactory* self)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "TextComponentFactory_Construct: self must not be NULL.");
    }

    Error Result = ObjectPool_Construct1(&self->_stringPool, sizeof(StringComponent), TEXT_COMPONENT_POOL_SECTION_CAPACITY);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = ObjectPool_Construct1(&self->_spritePool, sizeof(SpriteComponent), TEXT_COMPONENT_POOL_SECTION_CAPACITY);
    if (Result.Code != ErrorCode_Success)
    {
        Error Cleanup = ObjectPool_Deconstruct(&self->_stringPool);
        Error_Deconstruct(&Cleanup);
        return Result;
    }
    Result = ObjectPool_Construct1(&self->_emptyPool, sizeof(EmptyComponent), TEXT_COMPONENT_POOL_SECTION_CAPACITY);
    if (Result.Code != ErrorCode_Success)
    {
        Error Cleanup = ObjectPool_Deconstruct(&self->_stringPool);
        Error_Deconstruct(&Cleanup);
        Cleanup = ObjectPool_Deconstruct(&self->_spritePool);
        Error_Deconstruct(&Cleanup);
        return Result;
    }
    Result = BufferPool_Construct1(&self->_subComponentBuffers);
    if (Result.Code != ErrorCode_Success)
    {
        Error Cleanup = ObjectPool_Deconstruct(&self->_stringPool);
        Error_Deconstruct(&Cleanup);
        Cleanup = ObjectPool_Deconstruct(&self->_spritePool);
        Error_Deconstruct(&Cleanup);
        Cleanup = ObjectPool_Deconstruct(&self->_emptyPool);
        Error_Deconstruct(&Cleanup);
        return Result;
    }

    return Error_CreateSuccess();
}

Error TextComponentFactory_Deconstruct(TextComponentFactory* self)
{
    if (self == NULL)
    {
        return Error_CreateSuccess();
    }

    Error First = Error_CreateSuccess();
    AccumulateError(&First, ObjectPool_Deconstruct(&self->_stringPool));
    AccumulateError(&First, ObjectPool_Deconstruct(&self->_spritePool));
    AccumulateError(&First, ObjectPool_Deconstruct(&self->_emptyPool));
    AccumulateError(&First, BufferPool_Deconstruct(&self->_subComponentBuffers));
    return First;
}

Error TextComponentFactory_CreateEmpty(TextComponentFactory* self, EmptyComponent** outComponent)
{
    if ((self == NULL) || (outComponent == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextComponentFactory_CreateEmpty: self and outComponent must not be NULL.");
    }
    *outComponent = NULL;

    void* Slot = NULL;
    GenericBuffer* Buffer = NULL;
    Error Result = BorrowStorage(self, TextComponentType_Empty, &Slot, &Buffer);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    EmptyComponent* Component = Slot;
    Result = EmptyComponent_Construct(Component, Buffer);
    if (Result.Code != ErrorCode_Success)
    {
        ReturnStorage(self, TextComponentType_Empty, Slot, Buffer);
        return Result;
    }

    *outComponent = Component;
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
    GenericBuffer* Buffer = NULL;
    Error Result = BorrowStorage(self, TextComponentType_String, &Slot, &Buffer);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    StringComponent* Component = Slot;
    Result = StringComponent_Construct(Component, Buffer, text);
    if (Result.Code != ErrorCode_Success)
    {
        ReturnStorage(self, TextComponentType_String, Slot, Buffer);
        return Result;
    }

    *outComponent = Component;
    return Error_CreateSuccess();
}

Error TextComponentFactory_CreateStringStyled(TextComponentFactory* self,
    const unsigned char* text,
    GameFont font,
    RenderColor color,
    float size,
    StringComponent** outComponent)
{
    Error Result = TextComponentFactory_CreateString(self, text, outComponent);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Error StyleResult = ApplyStringStyle(*outComponent, font, color, size);
    if (StyleResult.Code != ErrorCode_Success)
    {
        Error ReturnResult = TextComponentFactory_ReturnComponent(self, &(*outComponent)->Base);
        Error_Deconstruct(&ReturnResult);
        *outComponent = NULL;
        return StyleResult;
    }
    return Error_CreateSuccess();
}

Error TextComponentFactory_CreateSprite(TextComponentFactory* self, SpriteAnimationInstance* animationInstance, SpriteComponent** outComponent)
{
    if ((self == NULL) || (outComponent == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextComponentFactory_CreateSprite: self and outComponent must not be NULL.");
    }
    *outComponent = NULL;

    void* Slot = NULL;
    GenericBuffer* Buffer = NULL;
    Error Result = BorrowStorage(self, TextComponentType_Sprite, &Slot, &Buffer);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    SpriteComponent* Component = Slot;
    Result = SpriteComponent_Construct(Component, Buffer, animationInstance);
    if (Result.Code != ErrorCode_Success)
    {
        ReturnStorage(self, TextComponentType_Sprite, Slot, Buffer);
        return Result;
    }

    *outComponent = Component;
    return Error_CreateSuccess();
}

Error TextComponentFactory_CreateSpriteSized(TextComponentFactory* self,
    SpriteAnimationInstance* animationInstance,
    RenderColor color,
    Vector2 size,
    SpriteComponent** outComponent)
{
    Error Result = TextComponentFactory_CreateSprite(self, animationInstance, outComponent);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Error StyleResult = SpriteComponent_SetColor(*outComponent, color);
    if (StyleResult.Code == ErrorCode_Success)
    {
        StyleResult = SpriteComponent_SetSize(*outComponent, size);
    }
    if (StyleResult.Code != ErrorCode_Success)
    {
        Error ReturnResult = TextComponentFactory_ReturnComponent(self, &(*outComponent)->Base);
        Error_Deconstruct(&ReturnResult);
        *outComponent = NULL;
        return StyleResult;
    }
    return Error_CreateSuccess();
}

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

Error TextComponentFactory_CreateInt64(TextComponentFactory* self, int64_t value, GenericBuffer* destinationString, StringComponent** outComponent)
{
    return TextComponentFactory_CreateInt64Styled(self, value, NUMBER_BASE_10, false,
        (GameFont){ ._rayFont = GetFontDefault() }, RenderColor_White(), STRING_COMPONENT_DEFAULT_SIZE,
        destinationString, outComponent);
}

Error TextComponentFactory_CreateInt64Styled(TextComponentFactory* self,
    int64_t value,
    int32_t base,
    bool includePrefix,
    GameFont font,
    RenderColor color,
    float size,
    GenericBuffer* destinationString,
    StringComponent** outComponent)
{
    if ((self == NULL) || (outComponent == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextComponentFactory_CreateInt64Styled: self and outComponent must not be NULL.");
    }
    *outComponent = NULL;

    Error BufferCheck = ValidateDestinationBuffer(destinationString);
    if (BufferCheck.Code != ErrorCode_Success)
    {
        return BufferCheck;
    }

    size_t StartOffset = destinationString->_count;
    Error FormatResult = Number_Int64ToString(value, base, includePrefix, destinationString);
    if (FormatResult.Code != ErrorCode_Success)
    {
        return FormatResult;
    }

    return TextComponentFactory_CreateStringStyled(self, destinationString->_data + StartOffset, font, color, size, outComponent);
}

Error TextComponentFactory_CreateUInt64(TextComponentFactory* self, uint64_t value, GenericBuffer* destinationString, StringComponent** outComponent)
{
    return TextComponentFactory_CreateUInt64Styled(self, value, NUMBER_BASE_10, false,
        (GameFont){ ._rayFont = GetFontDefault() }, RenderColor_White(), STRING_COMPONENT_DEFAULT_SIZE,
        destinationString, outComponent);
}

Error TextComponentFactory_CreateUInt64Styled(TextComponentFactory* self,
    uint64_t value,
    int32_t base,
    bool includePrefix,
    GameFont font,
    RenderColor color,
    float size,
    GenericBuffer* destinationString,
    StringComponent** outComponent)
{
    if ((self == NULL) || (outComponent == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextComponentFactory_CreateUInt64Styled: self and outComponent must not be NULL.");
    }
    *outComponent = NULL;

    Error BufferCheck = ValidateDestinationBuffer(destinationString);
    if (BufferCheck.Code != ErrorCode_Success)
    {
        return BufferCheck;
    }

    size_t StartOffset = destinationString->_count;
    Error FormatResult = Number_UInt64ToString(value, base, includePrefix, destinationString);
    if (FormatResult.Code != ErrorCode_Success)
    {
        return FormatResult;
    }

    return TextComponentFactory_CreateStringStyled(self, destinationString->_data + StartOffset, font, color, size, outComponent);
}

Error TextComponentFactory_CreateDouble(TextComponentFactory* self, double value, GenericBuffer* destinationString, StringComponent** outComponent)
{
    return TextComponentFactory_CreateDoubleStyled(self, value, DecimalFormatOptions_CreateShortest(DecimalSeparator_Period),
        (GameFont){ ._rayFont = GetFontDefault() }, RenderColor_White(), STRING_COMPONENT_DEFAULT_SIZE,
        destinationString, outComponent);
}

Error TextComponentFactory_CreateDoubleStyled(TextComponentFactory* self,
    double value,
    DecimalFormatOptions options,
    GameFont font,
    RenderColor color,
    float size,
    GenericBuffer* destinationString,
    StringComponent** outComponent)
{
    if ((self == NULL) || (outComponent == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextComponentFactory_CreateDoubleStyled: self and outComponent must not be NULL.");
    }
    *outComponent = NULL;

    Error BufferCheck = ValidateDestinationBuffer(destinationString);
    if (BufferCheck.Code != ErrorCode_Success)
    {
        return BufferCheck;
    }

    size_t StartOffset = destinationString->_count;
    Error FormatResult = Number_DoubleToString(value, destinationString, options);
    if (FormatResult.Code != ErrorCode_Success)
    {
        return FormatResult;
    }

    return TextComponentFactory_CreateStringStyled(self, destinationString->_data + StartOffset, font, color, size, outComponent);
}

Error TextComponentFactory_CreateCodepoint(TextComponentFactory* self, CodePoint codepoint, GenericBuffer* destinationString, StringComponent** outComponent)
{
    return TextComponentFactory_CreateCodepointStyled(self, codepoint,
        (GameFont){ ._rayFont = GetFontDefault() }, RenderColor_White(), STRING_COMPONENT_DEFAULT_SIZE,
        destinationString, outComponent);
}

Error TextComponentFactory_CreateCodepointStyled(TextComponentFactory* self,
    CodePoint codepoint,
    GameFont font,
    RenderColor color,
    float size,
    GenericBuffer* destinationString,
    StringComponent** outComponent)
{
    if ((self == NULL) || (outComponent == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextComponentFactory_CreateCodepointStyled: self and outComponent must not be NULL.");
    }
    *outComponent = NULL;

    Error BufferCheck = ValidateDestinationBuffer(destinationString);
    if (BufferCheck.Code != ErrorCode_Success)
    {
        return BufferCheck;
    }
    if (!CharUTF8_IsCodePointValid(codepoint))
    {
        return Error_Construct2(ErrorCode_InvalidCodePoint,
            "TextComponentFactory_CreateCodepointStyled: codepoint is not a valid Unicode scalar value.");
    }

    size_t StartOffset = destinationString->_count;
    unsigned char Encoded[4];
    size_t EncodedLength = CharUTF8_WriteCodePoint(Encoded, codepoint);
    if ((EncodedLength == 0)
        || !GenericBuffer_AppendRangeBytes(destinationString, Encoded, EncodedLength)
        || !GenericBuffer_NullTerminate(destinationString))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge,
            "TextComponentFactory_CreateCodepointStyled: could not append encoded codepoint to the buffer.");
    }

    return TextComponentFactory_CreateStringStyled(self, destinationString->_data + StartOffset, font, color, size, outComponent);
}

Error TextComponentFactory_CloneComponent(TextComponentFactory* self, const TextComponent* source, TextComponent** outComponent)
{
    if ((self == NULL) || (source == NULL) || (outComponent == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextComponentFactory_CloneComponent: self, source and outComponent must not be NULL.");
    }
    *outComponent = NULL;

    TextComponentType Type = source->Type;
    size_t StructSize = StructSizeForType(Type);
    if (StructSize == 0)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "TextComponentFactory_CloneComponent: unknown component type.");
    }

    void* Slot = NULL;
    GenericBuffer* Buffer = NULL;
    Error Result = BorrowStorage(self, Type, &Slot, &Buffer);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    // Shallow-copy every concrete field (borrowed text/sprite pointers are shared, not deep-cloned), then
    // re-point the base at our own empty subcomponent buffer (the copy clobbered it with the source's).
    Memory_Copy(source, Slot, StructSize);
    TextComponent* Clone = Slot;
    Clone->_subComponents = Buffer;

    size_t ChildCount = TextComponent_GetSubComponentCount(source);
    for (size_t Index = 0; Index < ChildCount; Index++)
    {
        const TextComponent* Child = TextComponent_GetSubComponentAt(source, Index);
        TextComponent* ChildClone = NULL;
        Result = TextComponentFactory_CloneComponent(self, Child, &ChildClone);
        if (Result.Code != ErrorCode_Success)
        {
            Error Cleanup = TextComponentFactory_ReturnComponentTree(self, Clone);
            Error_Deconstruct(&Cleanup);
            return Result;
        }

        Result = TextComponent_AddSubComponent(Clone, ChildClone);
        if (Result.Code != ErrorCode_Success)
        {
            Error Cleanup = TextComponentFactory_ReturnComponentTree(self, ChildClone);
            Error_Deconstruct(&Cleanup);
            Cleanup = TextComponentFactory_ReturnComponentTree(self, Clone);
            Error_Deconstruct(&Cleanup);
            return Result;
        }
    }

    *outComponent = Clone;
    return Error_CreateSuccess();
}

Error TextComponentFactory_ReturnComponent(TextComponentFactory* self, TextComponent* component)
{
    if ((self == NULL) || (component == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextComponentFactory_ReturnComponent: self and component must not be NULL.");
    }

    ObjectPool* Pool = GetPoolForType(self, component->Type);
    if (Pool == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "TextComponentFactory_ReturnComponent: unknown component type.");
    }

    // Capture the buffer before Destroy clears the base fields.
    GenericBuffer* Buffer = component->_subComponents;
    component->VTable->Destroy(component);

    Error First = Error_CreateSuccess();
    AccumulateError(&First, BufferPool_Return(&self->_subComponentBuffers, Buffer));
    AccumulateError(&First, ObjectPool_DisposeObject(Pool, component));
    return First;
}

Error TextComponentFactory_ReturnComponentTree(TextComponentFactory* self, TextComponent* root)
{
    if ((self == NULL) || (root == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextComponentFactory_ReturnComponentTree: self and root must not be NULL.");
    }

    Error First = Error_CreateSuccess();

    // Returning a child does not modify root's list, so indexing over the current children is stable.
    size_t ChildCount = TextComponent_GetSubComponentCount(root);
    for (size_t Index = 0; Index < ChildCount; Index++)
    {
        TextComponent* Child = TextComponent_GetSubComponentAt(root, Index);
        if (Child != NULL)
        {
            AccumulateError(&First, TextComponentFactory_ReturnComponentTree(self, Child));
        }
    }

    AccumulateError(&First, TextComponentFactory_ReturnComponent(self, root));
    return First;
}
