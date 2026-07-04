#include "SpriteSheet.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"
#include <stddef.h>


// Static functions.
static Error CreateNullError(const unsigned char* parameterName)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"Parameter \"%s\" cannot be null",
        parameterName);
}


// Functions.
Error SpriteSheet_Construct1(SpriteSheet* self, Texture2D texture, GenericBuffer* entries)
{
    if (self == NULL)
    {
        return CreateNullError(u8"self");
    }
    if (entries == NULL)
    {
        return CreateNullError(u8"entries");
    }

    Memory_Zero(self, sizeof(*self));
    self->_texture = texture;
    self->_entries = entries;

    return Error_CreateSuccess();
}

Error SpriteSheet_Deconstruct(SpriteSheet* self)
{
    if (self == NULL)
    {
        return CreateNullError(u8"self");
    }

    Memory_Zero(self, sizeof(*self));

    return Error_CreateSuccess();
}

Error SpriteSheet_GetTextureArea(SpriteSheet* self, const unsigned char* entryName, Rectangle* outArea)
{
    if (self == NULL)
    {
        return CreateNullError(u8"self");
    }

    Memory_Zero(outArea, sizeof(*outArea));

    GenericBuffer* Entries = self->_entries;
    for (size_t i = 0; i < Entries->_count; i++)
    {
        SpriteSheetEntry* Entry = GenericBuffer_GetPointerToElement(Entries, i);
        if (Entry == NULL)
        {
            return Error_Construct3(ErrorCode_InvalidState,
                u8"No sprite sheet element found at index %zu (internal error)",
                i);
        }

        bool IsCorrectEntry;
        Error Result = StringUTF8_EqualsExact(entryName, Entry->_name, &IsCorrectEntry);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        if (IsCorrectEntry)
        {
            *outArea = Entry->_textureArea;
            return Error_CreateSuccess();
        }
    }

    return Error_Construct3(ErrorCode_InvalidOperation,
        u8"Sprite sheet entry with the name \"%s\" not found.",
        entryName);
}
