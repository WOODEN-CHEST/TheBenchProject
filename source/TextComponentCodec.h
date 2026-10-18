#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "TextComponent.h"
#include "TextComponentFactory.h"
#include "wr/WRJSON.h"
#include "wr/WRGHDF.h"
#include "wr/WRError.h"


/*
 * The one text component codec behind both serialized forms (private; TextComponentJSON.c and TextComponentGHDF.c are
 * its only users).
 *
 * Every component type's fields are listed ONCE, in the tables in TextComponentCodec.c: the JSON key (with an
 * optional enclosing group such as "shadow"), the GHDF entry id, the value kind, whether it is required, and the
 * valid range. The codec walks the tree, validates every value against its field, applies it through the components'
 * setters and builds the error messages (naming the field and the component's path). A format adapter only moves
 * values in and out of its document tree through the ITextCodecReader / ITextCodecWriter interfaces, so the two
 * formats cannot drift apart.
 *
 * Nesting is limited to TEXT_COMPONENT_MAX_DEPTH levels while decoding, before any deeper node is read.
 */


// Types.
/* How a field's value is shaped. */
typedef enum TextCodecValueKindEnum
{
    TextCodecValueKind_Text,        /* UTF-8 text without NUL bytes */
    TextCodecValueKind_Name,        /* an asset name or translation key ([a-z][a-z0-9_.-]*) */
    TextCodecValueKind_Decimal,     /* a finite decimal within the field's range */
    TextCodecValueKind_Boolean,
    TextCodecValueKind_RenderColor, /* brightness and opacity within [0, 1] */
    TextCodecValueKind_Vector2,     /* two finite decimals within the field's range */
    TextCodecValueKind_ShadowColor, /* the default shadow color, or a custom render color */
    TextCodecValueKind_ShadowOffset /* the font's shadow offset, or a custom vector within the field's range */
} TextCodecValueKind;

/* Identifies the component property a field reads and writes. */
typedef enum TextCodecFieldIdEnum
{
    TextCodecFieldId_Text,
    TextCodecFieldId_Fallback,
    TextCodecFieldId_Key,
    TextCodecFieldId_Font,
    TextCodecFieldId_Color,
    TextCodecFieldId_Size,
    TextCodecFieldId_ShadowActive,
    TextCodecFieldId_ShadowColor,
    TextCodecFieldId_ShadowOffset,
    TextCodecFieldId_Underline,
    TextCodecFieldId_Strikethrough,
    TextCodecFieldId_Spacing,
    TextCodecFieldId_Animation,
    TextCodecFieldId_SpriteColor,
    TextCodecFieldId_SpriteSize
} TextCodecFieldId;

/* One serialized field of a component type. */
typedef struct TextCodecFieldStruct
{
    TextCodecFieldId Id;
    TextCodecValueKind Kind;
    /* JSON: the key of the enclosing compound (e.g. u8"shadow"), or NULL for a key directly in the component. */
    const unsigned char* JsonGroup;
    /* JSON: the key inside the component (or inside JsonGroup). */
    const unsigned char* JsonKey;
    /* The name error messages use, e.g. u8"shadow.offset". */
    const unsigned char* DisplayName;
    /* GHDF: the entry id inside the component compound. */
    GHDFEntryID GhdfId;
    /* A required field must be present when reading and is always written. */
    bool IsRequired;
    /* Decimal and Vector2 fields: the inclusive range of every component. */
    float Minimum;
    float Maximum;
} TextCodecField;

/* A field value moving between the codec and an adapter. Only the members of the field's kind are meaningful. */
typedef struct TextCodecValueStruct
{
    /* Text and Name: NUL-terminated UTF-8. When read, borrowed from the adapter until its next read; when written,
     * borrowed from the component. A Name may be NULL when written (an unset font or animation). */
    const unsigned char* Text;
    float Decimal;
    bool Boolean;
    /* RenderColor; for ShadowColor the custom color (when IsCustom). */
    RenderColor Color;
    /* Vector2; for ShadowOffset the custom offset (when IsCustom). */
    Vector2 Vector;
    /* ShadowColor and ShadowOffset: true for a custom Color or Vector, false for the default (the shadow color derived
     * from the text color, the shadow offset of the text's font). */
    bool IsCustom;
} TextCodecValue;

/* A node of a serialized tree. The JSON adapter uses Json, the GHDF adapter Ghdf. */
typedef struct TextCodecNodeStruct
{
    JSONObjectValue Json;
    GHDFCompound* Ghdf;
} TextCodecNode;

/* The shape a node is written in. Only JSON has the shorthands. */
typedef enum TextCodecNodeShapeEnum
{
    TextCodecNodeShape_Object,    /* a component with a type, fields and an optional children list */
    TextCodecNodeShape_Text,      /* shorthand: a string component with default styling */
    TextCodecNodeShape_List       /* shorthand: an empty component whose children are the list's entries */
} TextCodecNodeShape;

/* What an adapter reports about a node before its fields and children are read. */
typedef struct TextCodecNodeInfoStruct
{
    TextCodecNodeShape Shape;
    /* Object: the component type. */
    TextComponentType Type;
    /* Text: the NUL-terminated text, borrowed from the adapter until its next read. */
    const unsigned char* Text;
    /* Object: number of children; List: number of entries. */
    size_t ChildCount;
} TextCodecNodeInfo;

/* Reads one serialized tree. An error message describes only the problem; the codec adds the field and the path. */
typedef struct ITextCodecReaderVTableStruct
{
    /* Classifies a node and reads its type and child count. */
    Error (*ReadNodeInfo)(void* self, const TextCodecNode* node, TextCodecNodeInfo* outInfo);
    /* Reads one field of an object node; *outFound is false when the field is absent. */
    Error (*ReadField)(void* self, const TextCodecNode* node, const TextCodecField* field, TextCodecValue* outValue,
        bool* outFound);
    /* Gets child number index of an object node, or entry number index of a list node. */
    Error (*GetChild)(void* self, const TextCodecNode* node, size_t index, TextCodecNode* outChild);
    /* Releases the reader's working memory. */
    void (*Destroy)(void* self);
} ITextCodecReaderVTable;

typedef struct ITextCodecReaderStruct
{
    const ITextCodecReaderVTable* VTable;
    void* Self;
} ITextCodecReader;

/* Builds one serialized tree. Nodes are owned by the writer's pool; a node handed to AppendChild belongs to its
 * parent from then on, and DiscardNode releases a node that was never attached (with everything below it). */
typedef struct ITextCodecWriterVTableStruct
{
    /* Whether the format has the text and list shorthands. */
    bool (*AreShorthandsSupported)(void* self);
    /* Creates an object node of the given type (the type is written). */
    Error (*CreateObject)(void* self, TextComponentType type, TextCodecNode* outNode);
    /* Creates a text shorthand node. */
    Error (*CreateTextShorthand)(void* self, const unsigned char* text, TextCodecNode* outNode);
    /* Creates an empty list shorthand node. */
    Error (*CreateListShorthand)(void* self, TextCodecNode* outNode);
    /* Writes one field into an object node. */
    Error (*WriteField)(void* self, TextCodecNode* node, const TextCodecField* field, const TextCodecValue* value);
    /* Appends child as the last child of an object node (its children list) or the last entry of a list node. On
     * success the child belongs to the parent; on failure it is still the caller's. */
    Error (*AppendChild)(void* self, TextCodecNode* parent, TextCodecNode* child);
    /* Releases an unattached node and everything below it. */
    void (*DiscardNode)(void* self, TextCodecNode* node);
    /* Releases the writer's working memory. */
    void (*Destroy)(void* self);
} ITextCodecWriterVTable;

typedef struct ITextCodecWriterStruct
{
    const ITextCodecWriterVTable* VTable;
    void* Self;
} ITextCodecWriter;


// Functions.
/*
 * Decodes the tree at root into new components of factory. Every bad value (wrong shape, out of range, unknown type,
 * missing required field, nesting deeper than TEXT_COMPONENT_MAX_DEPTH) fails with failureCode, and the message names
 * the field and the component's path (JSON pointer style, e.g. "/children/2"). On failure nothing is left allocated
 * and *outRoot is NULL; on success *outRoot is a root owned by the caller.
 */
Error TextComponentCodec_Decode(ITextCodecReader reader, ErrorCode failureCode, const TextCodecNode* root,
    TextComponentFactory* factory, TextComponent** outRoot);

/*
 * Encodes a component tree. Fields with their default value are left out (required fields are always written); with
 * shorthands, a default-styled childless string component becomes a text node and an empty component a list node.
 * On failure every node built so far is discarded.
 */
Error TextComponentCodec_Encode(ITextCodecWriter writer, const TextComponent* root, TextCodecNode* outNode);

/* The JSON "type" name of a component type (u8"string", ...). */
const unsigned char* TextComponentCodec_GetTypeName(TextComponentType type);

/* Finds the component type with the given JSON "type" name (byteCount bytes, not NUL-terminated). */
bool TextComponentCodec_FindTypeByName(const unsigned char* name, size_t byteCount, TextComponentType* outType);

/* The GHDF wire id of a component type; explicit constants, independent of the enum's values. */
uint8_t TextComponentCodec_GetTypeWireId(TextComponentType type);

/* Finds the component type with the given GHDF wire id. */
bool TextComponentCodec_FindTypeByWireId(uint8_t wireId, TextComponentType* outType);

/* The accepted JSON type names as a readable list for messages: "\"empty\", \"string\", \"sprite\" or \"translate\"". */
const unsigned char* TextComponentCodec_GetTypeNameList(void);
