#include "TextComponentText.h"


// Static functions.
static Error AppendComponentText(const TextComponent* component, GenericBuffer* destination)
{
    if (component->Type == TextComponentType_String)
    {
        const StringComponent* StringComp = (const StringComponent*)component;
        if (StringComp->_text != NULL)
        {
            if (!GenericBuffer_AppendString(destination, StringComp->_text))
            {
                return Error_Construct2(ErrorCode_BufferTooLarge,
                    "TextComponentText_Serialize: could not append text to the destination buffer.");
            }
        }
    }

    size_t ChildCount = TextComponent_GetSubComponentCount(component);
    for (size_t Index = 0; Index < ChildCount; Index++)
    {
        const TextComponent* Child = TextComponent_GetSubComponentAt(component, Index);
        if (Child == NULL)
        {
            continue;
        }
        Error Result = AppendComponentText(Child, destination);
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
