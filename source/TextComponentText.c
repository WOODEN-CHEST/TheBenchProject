#include "TextComponentText.h"


// Static functions.
// The text a component contributes itself: a string's text, a translate component's fallback (or its key when it
// has none, the same text a document without translations shows); nothing for sprites and empty components.
static const unsigned char* GetOwnText(const TextComponent* component)
{
    switch (TextComponent_GetType(component))
    {
        case TextComponentType_String:
            return StringComponent_GetText(TextComponent_AsConstStringComponent(component));
        case TextComponentType_Translate:
        {
            const TranslateComponent* Translate = TextComponent_AsConstTranslateComponent(component);
            const unsigned char* Fallback = TranslateComponent_GetFallback(Translate);
            return (Fallback[0] != (unsigned char)0) ? Fallback : TranslateComponent_GetKey(Translate);
        }
        case TextComponentType_Sprite:
        case TextComponentType_Empty:
            return NULL;
    }
    return NULL;
}

// Recursion depth is bounded by the tree's depth limit (TEXT_COMPONENT_MAX_DEPTH).
static Error AppendComponentText(const TextComponent* component, GenericBuffer* destination)
{
    const unsigned char* OwnText = GetOwnText(component);
    if ((OwnText != NULL) && !GenericBuffer_AppendString(destination, OwnText))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge,
            "TextComponentText_Serialize: could not append text to the destination buffer.");
    }

    size_t ChildCount = TextComponent_GetSubComponentCount(component);
    for (size_t Index = 0; Index < ChildCount; Index++)
    {
        Error Result = AppendComponentText(TextComponent_GetSubComponentAt(component, Index), destination);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    return Error_CreateSuccess();
}


// Public functions.
Error TextComponentText_Serialize(const TextComponent* component, GenericBuffer* destination)
{
    if ((component == NULL) || (destination == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextComponentText_Serialize: component and destination must not be NULL.");
    }
    if (destination->_elementSize != sizeof(unsigned char))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextComponentText_Serialize: destination must be a byte buffer (element size 1).");
    }

    return AppendComponentText(component, destination);
}
