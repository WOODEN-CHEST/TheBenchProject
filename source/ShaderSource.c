#include <inttypes.h>
#include "ShaderSource.h"
#include "AssetDefinitionCommon.h"
#include "wr/WRIO.h"
#include "wr/WRMemory.h"
#include "wr/WRNumber.h"


// Macros.
/** Source-string number of the main file: the one string raylib hands to glShaderSource. */
#define SHADER_SOURCE_MAIN_INDEX ((size_t)0)


// Types.
typedef enum ShaderDirectiveEnum
{
    ShaderDirective_None,
    ShaderDirective_Include,
    ShaderDirective_Version
} ShaderDirective;

/* One line of a source file. */
typedef struct ShaderLineStruct
{
    const unsigned char* Text; // the line's first byte
    size_t ContentLength;      // bytes before the line break, a '\r' before the '\n' excluded
    size_t FullLength;         // bytes including the line break
    bool HasLineBreak;         // false only for a last line without '\n'
    bool HasNul;               // a NUL byte would end the source string handed to the GL early
    size_t Number;             // 1-based
} ShaderLine;

/* One expansion. Source i (its GLSL source-string number) is named by the NUL-terminated string at
 * SourceNames->_data + SourceOffsets[i]; source 0 is the main file. Names are looked up by index whenever they are
 * needed, because recording a new source can move SourceNames. */
typedef struct ShaderExpansionStruct
{
    AssetManager* Assets;
    AssetTypeID ShaderType;
    GenericBuffer* Output;        // the caller's
    GenericBuffer* SourceNames;   // borrowed from Assets (bytes)
    GenericBuffer* SourceOffsets; // borrowed from Assets (size_t)
    bool IsMainFile;              // source 0 is a file, so including it again is a repeat
    size_t MainVersionLine;       // line of the main file's #version, 0 when it has none
} ShaderExpansion;

/* A byte buffer's state before an append to it as one growing string, to undo the append on failure. */
typedef struct GrowingStringMarkStruct
{
    size_t Count;  // the count without the trailing NUL
    bool HadNul;   // whether the string was NUL-terminated
} GrowingStringMark;


// Static functions.
static bool IsHorizontalSpace(unsigned char character)
{
    return (character == ' ') || (character == '\t');
}

static size_t SkipHorizontalSpace(const unsigned char* text, size_t length, size_t index)
{
    while ((index < length) && IsHorizontalSpace(text[index]))
    {
        index++;
    }
    return index;
}

/* Whether the line is the directive @p name: '#', optional spaces, the name, then a space, a quote or the end of the
 * line. On a match *outNameEnd is the index just past the name. */
static bool MatchDirective(const unsigned char* line, size_t length, const unsigned char* name, size_t* outNameEnd)
{
    size_t Index = SkipHorizontalSpace(line, length, 0U);
    if ((Index >= length) || (line[Index] != '#'))
    {
        return false;
    }
    Index = SkipHorizontalSpace(line, length, Index + 1U);
    for (size_t NameIndex = 0U; name[NameIndex] != '\0'; NameIndex++)
    {
        if ((Index >= length) || (line[Index] != name[NameIndex]))
        {
            return false;
        }
        Index++;
    }
    if ((Index < length) && !IsHorizontalSpace(line[Index]) && (line[Index] != '"'))
    {
        return false;
    }
    *outNameEnd = Index;
    return true;
}

/* Classifies a line by its content (without the line break); *outNameEnd is set for an #include. */
static ShaderDirective ClassifyLine(const unsigned char* line, size_t length, size_t* outNameEnd)
{
    if (MatchDirective(line, length, u8"include", outNameEnd))
    {
        return ShaderDirective_Include;
    }
    size_t NameEnd = 0U;
    if (MatchDirective(line, length, u8"version", &NameEnd))
    {
        return ShaderDirective_Version;
    }
    return ShaderDirective_None;
}

/* Reads the line that starts at @p start (which must be below @p length). */
static ShaderLine ReadLine(const unsigned char* text, size_t length, size_t start, size_t number)
{
    ShaderLine Line;
    Line.HasNul = false;
    size_t End = start;
    while ((End < length) && (text[End] != '\n'))
    {
        Line.HasNul = Line.HasNul || (text[End] == '\0');
        End++;
    }
    Line.Text = text + start;
    Line.HasLineBreak = (End < length);
    Line.FullLength = (End - start) + (Line.HasLineBreak ? 1U : 0U);
    Line.ContentLength = End - start;
    if ((Line.ContentLength > 0U) && (Line.Text[Line.ContentLength - 1U] == '\r'))
    {
        Line.ContentLength--;
    }
    Line.Number = number;
    return Line;
}

/* Returns the line number of the first #version line, or 0 when the text has none. */
static size_t FindVersionLine(const unsigned char* text, size_t length)
{
    size_t Start = 0U;
    size_t Number = 1U;
    while (Start < length)
    {
        ShaderLine Line = ReadLine(text, length, Start, Number);
        size_t NameEnd = 0U;
        if (ClassifyLine(Line.Text, Line.ContentLength, &NameEnd) == ShaderDirective_Version)
        {
            return Number;
        }
        Start += Line.FullLength;
        Number++;
    }
    return 0U;
}

/* Finds the quoted location of an #include line after its directive name: optional spaces, "location", then only
 * spaces or a // comment. One leading '/' or '\' is dropped, as the resolver ignores it. False when malformed. */
static bool ParseIncludeLocation(const ShaderLine* line, size_t nameEnd, size_t* outStart, size_t* outLength)
{
    size_t Index = SkipHorizontalSpace(line->Text, line->ContentLength, nameEnd);
    if ((Index >= line->ContentLength) || (line->Text[Index] != '"'))
    {
        return false;
    }
    size_t Start = Index + 1U;
    size_t End = Start;
    while ((End < line->ContentLength) && (line->Text[End] != '"'))
    {
        End++;
    }
    if (End >= line->ContentLength)
    {
        return false;
    }

    Index = SkipHorizontalSpace(line->Text, line->ContentLength, End + 1U);
    bool IsCommentNext = ((Index + 1U) < line->ContentLength) && (line->Text[Index] == '/')
        && (line->Text[Index + 1U] == '/');
    if ((Index < line->ContentLength) && !IsCommentNext)
    {
        return false;
    }

    if ((Start < End) && ((line->Text[Start] == '/') || (line->Text[Start] == '\\')))
    {
        Start++;
    }
    if (Start == End)
    {
        return false;
    }
    *outStart = Start;
    *outLength = End - Start;
    return true;
}

static size_t GetSourceCount(const ShaderExpansion* self)
{
    return self->SourceOffsets->_count;
}

static size_t GetSourceOffset(const ShaderExpansion* self, size_t index)
{
    size_t Offset = 0U;
    if (!GenericBuffer_GetAt(self->SourceOffsets, index, &Offset))
    {
        return 0U;
    }
    return Offset;
}

static const unsigned char* GetSourceName(const ShaderExpansion* self, size_t index)
{
    return self->SourceNames->_data + GetSourceOffset(self, index);
}

static size_t GetSourceNameLength(const ShaderExpansion* self, size_t index)
{
    size_t End = ((index + 1U) < GetSourceCount(self)) ? GetSourceOffset(self, index + 1U) : self->SourceNames->_count;
    return End - GetSourceOffset(self, index) - 1U; // without the NUL
}

static Error AddSource(ShaderExpansion* self, const unsigned char* name, size_t nameLength, size_t* outIndex)
{
    size_t Offset = self->SourceNames->_count;
    // GenericBuffer_AppendRangeBytes takes a mutable pointer but only reads it.
    if (!GenericBuffer_AppendRangeBytes(self->SourceNames, (unsigned char*)name, nameLength)
        || !GenericBuffer_AppendByte(self->SourceNames, 0U)
        || !GenericBuffer_AddLast(self->SourceOffsets, &Offset))
    {
        return Error_Construct3(ErrorCode_BufferTooLarge, u8"Failed to record a shader source name.");
    }
    *outIndex = GetSourceCount(self) - 1U;
    return Error_CreateSuccess();
}

/* Whether a file with this location was included already (or is the main file, when that is a file). */
static bool IsSourceIncluded(const ShaderExpansion* self, const unsigned char* name, size_t nameLength)
{
    size_t First = self->IsMainFile ? SHADER_SOURCE_MAIN_INDEX : (SHADER_SOURCE_MAIN_INDEX + 1U);
    for (size_t Index = First; Index < GetSourceCount(self); Index++)
    {
        if ((GetSourceNameLength(self, Index) == nameLength)
            && Memory_IsEqual(GetSourceName(self, Index), name, nameLength))
        {
            return true;
        }
    }
    return false;
}

static Error AppendOutput(ShaderExpansion* self, const unsigned char* bytes, size_t count)
{
    // GenericBuffer_AppendRangeBytes takes a mutable pointer but only reads it.
    if (!GenericBuffer_AppendRangeBytes(self->Output, (unsigned char*)bytes, count))
    {
        return Error_Construct3(ErrorCode_BufferTooLarge, u8"Failed to grow the expanded shader source.");
    }
    return Error_CreateSuccess();
}

static Error AppendOutputString(ShaderExpansion* self, const unsigned char* text)
{
    if (!GenericBuffer_AppendString(self->Output, text))
    {
        return Error_Construct3(ErrorCode_BufferTooLarge, u8"Failed to grow the expanded shader source.");
    }
    return Error_CreateSuccess();
}

/* Appends the decimal digits of @p value. The WRNumber writer appends a NUL after the digits and counts it (its
 * header says it does not count it), so a trailing NUL is dropped again. */
static bool AppendDecimal(GenericBuffer* buffer, size_t value)
{
    Error Result = Number_UInt64ToString((uint64_t)value, NUMBER_BASE_10, false, buffer);
    bool IsAppended = (Result.Code == ErrorCode_Success);
    Error_Deconstruct(&Result);
    if (IsAppended && (buffer->_count > 0U) && (buffer->_data[buffer->_count - 1U] == '\0'))
    {
        GenericBuffer_RemoveLast(buffer);
    }
    return IsAppended;
}

/* Writes "#line <lineNumber> <sourceIndex>\n". */
static Error AppendLineDirective(ShaderExpansion* self, size_t lineNumber, size_t sourceIndex)
{
    bool IsAppended = GenericBuffer_AppendString(self->Output, u8"#line ") && AppendDecimal(self->Output, lineNumber)
        && GenericBuffer_AppendString(self->Output, u8" ") && AppendDecimal(self->Output, sourceIndex)
        && GenericBuffer_AppendString(self->Output, u8"\n");
    if (!IsAppended)
    {
        return Error_Construct3(ErrorCode_BufferTooLarge, u8"Failed to grow the expanded shader source.");
    }
    return Error_CreateSuccess();
}

/* Reads an included file into a buffer borrowed from the manager, which the caller returns. A file that cannot be
 * found or read gives an error naming the include, the including file and the line. */
static Error ReadInclude(ShaderExpansion* self, size_t includeIndex, size_t parentIndex, size_t lineNumber,
    GenericBuffer** outText)
{
    *outText = NULL;
    AssetLocation Location;
    Location.Type = AssetLocationType_File;
    Location.Value = GetSourceName(self, includeIndex);

    IOStream* Stream = NULL;
    Error Result = AssetManager_OpenResource(self->Assets, self->ShaderType, &Location, &ASSET_EXTENSIONS_SHADER_STAGE,
        &Stream);
    if (Result.Code == ErrorCode_Success)
    {
        GenericBuffer* Text = NULL;
        Result = AssetManager_BorrowGenericBuffer(self->Assets, sizeof(unsigned char), &Text);
        if (Result.Code == ErrorCode_Success)
        {
            Result = IOStream_ReadAll(Stream, Text);
            if (Result.Code == ErrorCode_Success)
            {
                *outText = Text;
            }
            else
            {
                Error ReturnResult = AssetManager_ReturnGenericBuffer(self->Assets, Text);
                Error_Deconstruct(&ReturnResult);
            }
        }
        Error CloseResult = AssetManager_CloseResource(self->Assets, Stream);
        Error_Deconstruct(&CloseResult);
    }
    if (Result.Code == ErrorCode_Success)
    {
        return Result;
    }

    Error Wrapped = Error_Construct3(ErrorCode_InvalidAssetData,
        u8"Shader include \"%s\" at line %" PRIu64 u8" of \"%s\" could not be read: %s",
        GetSourceName(self, includeIndex), (uint64_t)lineNumber, GetSourceName(self, parentIndex),
        (Result.Message != NULL) ? Result.Message : (const unsigned char*)u8"(no details)");
    Error_Deconstruct(&Result);
    return Wrapped;
}

static Error ExpandFile(ShaderExpansion* self, const unsigned char* text, size_t length, size_t sourceIndex,
    size_t depth);

/* Replaces an #include line: the included file's expanded text between two #line directives, or an empty line when
 * the file was included before. The recursion ends at SHADER_SOURCE_MAX_INCLUDE_DEPTH. */
static Error ExpandInclude(ShaderExpansion* self, const ShaderLine* line, const unsigned char* location,
    size_t locationLength, size_t sourceIndex, size_t depth)
{
    if (IsSourceIncluded(self, location, locationLength))
    {
        // An empty line keeps the including file's line numbers right.
        return line->HasLineBreak ? AppendOutputString(self, u8"\n") : Error_CreateSuccess();
    }

    size_t IncludeIndex = 0U;
    Error Result = AddSource(self, location, locationLength, &IncludeIndex);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (depth >= SHADER_SOURCE_MAX_INCLUDE_DEPTH)
    {
        return Error_Construct3(ErrorCode_InvalidAssetData,
            u8"Shader include \"%s\" at line %" PRIu64 u8" of \"%s\" is nested more than %" PRIu64 u8" levels deep.",
            GetSourceName(self, IncludeIndex), (uint64_t)line->Number, GetSourceName(self, sourceIndex),
            (uint64_t)SHADER_SOURCE_MAX_INCLUDE_DEPTH);
    }

    GenericBuffer* IncludedText = NULL;
    Result = ReadInclude(self, IncludeIndex, sourceIndex, line->Number, &IncludedText);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = AppendLineDirective(self, 1U, IncludeIndex);
    if (Result.Code == ErrorCode_Success)
    {
        Result = ExpandFile(self, IncludedText->_data, IncludedText->_count, IncludeIndex, depth + 1U);
    }
    Error ReturnResult = AssetManager_ReturnGenericBuffer(self->Assets, IncludedText);
    Error_Deconstruct(&ReturnResult);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    // The included file may end without a line break, and the #line directive must start a line of its own.
    if ((self->Output->_count == 0U) || (self->Output->_data[self->Output->_count - 1U] != '\n'))
    {
        Result = AppendOutputString(self, u8"\n");
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    return AppendLineDirective(self, line->Number + 1U, sourceIndex);
}

static Error ExpandIncludeLine(ShaderExpansion* self, const ShaderLine* line, size_t nameEnd, size_t sourceIndex,
    size_t depth)
{
    size_t LocationStart = 0U;
    size_t LocationLength = 0U;
    if (!ParseIncludeLocation(line, nameEnd, &LocationStart, &LocationLength))
    {
        return Error_Construct3(ErrorCode_InvalidAssetData,
            u8"Malformed #include at line %" PRIu64 u8" of \"%s\": write #include \"location\" (a shader file location "
            u8"without extension, optionally followed by a // comment).",
            (uint64_t)line->Number, GetSourceName(self, sourceIndex));
    }
    if ((depth == 0U) && (line->Number < self->MainVersionLine))
    {
        return Error_Construct3(ErrorCode_InvalidAssetData,
            u8"The #include at line %" PRIu64 u8" of \"%s\" comes before its #version line, which must stay first.",
            (uint64_t)line->Number, GetSourceName(self, sourceIndex));
    }
    return ExpandInclude(self, line, line->Text + LocationStart, LocationLength, sourceIndex, depth);
}

static Error ExpandLine(ShaderExpansion* self, const ShaderLine* line, size_t sourceIndex, size_t depth)
{
    if (line->HasNul)
    {
        return Error_Construct3(ErrorCode_InvalidAssetData,
            u8"Shader source \"%s\" contains a NUL byte at line %" PRIu64 u8".",
            GetSourceName(self, sourceIndex), (uint64_t)line->Number);
    }

    size_t NameEnd = 0U;
    ShaderDirective Directive = ClassifyLine(line->Text, line->ContentLength, &NameEnd);
    switch (Directive)
    {
        case ShaderDirective_None:
            return AppendOutput(self, line->Text, line->FullLength);
        case ShaderDirective_Version:
            if (depth > 0U)
            {
                return Error_Construct3(ErrorCode_InvalidAssetData,
                    u8"Shader include \"%s\" has a #version line (line %" PRIu64 u8"); only the main file declares the "
                    u8"version.", GetSourceName(self, sourceIndex), (uint64_t)line->Number);
            }
            return AppendOutput(self, line->Text, line->FullLength);
        case ShaderDirective_Include:
            return ExpandIncludeLine(self, line, NameEnd, sourceIndex, depth);
    }
    return Error_Construct3(ErrorCode_InvalidState, u8"Unknown shader directive kind %d.", (int)Directive);
}

static Error ExpandFile(ShaderExpansion* self, const unsigned char* text, size_t length, size_t sourceIndex,
    size_t depth)
{
    size_t Start = 0U;
    size_t Number = 1U;
    while (Start < length)
    {
        ShaderLine Line = ReadLine(text, length, Start, Number);
        Error Result = ExpandLine(self, &Line, sourceIndex, depth);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        Start += Line.FullLength;
        Number++;
    }
    return Error_CreateSuccess();
}

/* Starts appending to @p buffer as one growing string: drops a trailing NUL and remembers what was there, so
 * RestoreGrowingString can undo a failed append. */
static GrowingStringMark BeginGrowingString(GenericBuffer* buffer)
{
    GrowingStringMark Mark;
    Mark.HadNul = (buffer->_count > 0U) && (buffer->_data[buffer->_count - 1U] == '\0');
    if (Mark.HadNul)
    {
        GenericBuffer_RemoveLast(buffer);
    }
    Mark.Count = buffer->_count;
    return Mark;
}

static void RestoreGrowingString(GenericBuffer* buffer, GrowingStringMark mark)
{
    GenericBuffer_SetCount(buffer, mark.Count);
    if (mark.HadNul)
    {
        GenericBuffer_NullTerminate(buffer);
    }
}

/* Appends `0 = "main", 1 = "common/a", ...` to @p destination as one growing NUL-terminated string. */
static Error AppendSourceList(const ShaderExpansion* self, GenericBuffer* destination)
{
    GrowingStringMark Mark = BeginGrowingString(destination);
    bool IsAppended = true;
    for (size_t Index = 0U; IsAppended && (Index < GetSourceCount(self)); Index++)
    {
        if (Index > 0U)
        {
            IsAppended = GenericBuffer_AppendString(destination, u8", ");
        }
        IsAppended = IsAppended && AppendDecimal(destination, Index) && GenericBuffer_AppendString(destination, u8" = \"")
            && GenericBuffer_AppendString(destination, GetSourceName(self, Index))
            && GenericBuffer_AppendString(destination, u8"\"");
    }
    if (IsAppended && GenericBuffer_NullTerminate(destination))
    {
        return Error_CreateSuccess();
    }
    RestoreGrowingString(destination, Mark);
    return Error_Construct3(ErrorCode_BufferTooLarge, u8"Failed to list the shader's source files.");
}

/* Records source 0, the main file. A file location loses one leading '/' or '\', like include locations. */
static Error AddMainSource(ShaderExpansion* self, const AssetLocation* mainLocation)
{
    const unsigned char* Name = mainLocation->Value;
    if (self->IsMainFile && ((Name[0] == '/') || (Name[0] == '\\')))
    {
        Name++;
    }
    size_t Length = 0U;
    while (Name[Length] != '\0')
    {
        Length++;
    }
    size_t Index = 0U;
    return AddSource(self, Name, Length, &Index);
}


// Public functions.
Error ShaderSource_ExpandIncludes(AssetManager* assets, AssetTypeID shaderType, const AssetLocation* mainLocation,
    const unsigned char* text, size_t length, GenericBuffer* outText, GenericBuffer* outSourceList)
{
    if ((assets == NULL) || (mainLocation == NULL) || (mainLocation->Value == NULL) || (outText == NULL)
        || ((text == NULL) && (length > 0U)))
    {
        return Error_Construct3(ErrorCode_IllegalArgument,
            u8"ShaderSource_ExpandIncludes needs a manager, a main location, an output buffer and the text.");
    }

    ShaderExpansion Expansion;
    Memory_Zero(&Expansion, sizeof(Expansion));
    Expansion.Assets = assets;
    Expansion.ShaderType = shaderType;
    Expansion.Output = outText;
    Expansion.IsMainFile = (mainLocation->Type == AssetLocationType_File);
    Expansion.MainVersionLine = (text != NULL) ? FindVersionLine(text, length) : 0U;

    Error Result = AssetManager_BorrowGenericBuffer(assets, sizeof(unsigned char), &Expansion.SourceNames);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = AssetManager_BorrowGenericBuffer(assets, sizeof(size_t), &Expansion.SourceOffsets);
    if (Result.Code != ErrorCode_Success)
    {
        Error ReturnResult = AssetManager_ReturnGenericBuffer(assets, Expansion.SourceNames);
        Error_Deconstruct(&ReturnResult);
        return Result;
    }

    GrowingStringMark OutputMark = BeginGrowingString(outText);
    Result = AddMainSource(&Expansion, mainLocation);
    if ((Result.Code == ErrorCode_Success) && (text != NULL))
    {
        Result = ExpandFile(&Expansion, text, length, SHADER_SOURCE_MAIN_INDEX, 0U);
    }
    if ((Result.Code == ErrorCode_Success) && !GenericBuffer_NullTerminate(outText))
    {
        Result = Error_Construct3(ErrorCode_BufferTooLarge, u8"Failed to terminate the expanded shader source.");
    }
    if ((Result.Code == ErrorCode_Success) && (outSourceList != NULL) && (GetSourceCount(&Expansion) > 1U))
    {
        Result = AppendSourceList(&Expansion, outSourceList);
    }
    if (Result.Code != ErrorCode_Success)
    {
        RestoreGrowingString(outText, OutputMark);
    }

    Error NamesResult = AssetManager_ReturnGenericBuffer(assets, Expansion.SourceNames);
    Error_Deconstruct(&NamesResult);
    Error OffsetsResult = AssetManager_ReturnGenericBuffer(assets, Expansion.SourceOffsets);
    Error_Deconstruct(&OffsetsResult);
    return Result;
}
