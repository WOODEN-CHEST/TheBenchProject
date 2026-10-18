#include "TestSuites.h"
#include "TestText.h"
#include "TextComponent.h"
#include "TextComponentFactory.h"
#include "TextComponentJSON.h"
#include "TextComponentGHDF.h"
#include "wr/WRJSON.h"
#include "wr/WRGHDF.h"
#include "wr/WRMemoryStream.h"
#include <string.h>


// Static functions: fixtures.
// A tree where every field of every type differs from its default. Its decimals include ones that need more than 6
// significant digits (1/3) and ones that are not exact in binary (0.1), so JSON text must keep floats exactly.
static TextComponent* BuildRichTree(TestRun* run, TextComponentFactory* factory)
{
    StringComponent* Root = NULL;
    TranslateComponent* Title = NULL;
    SpriteComponent* Coin = NULL;
    EmptyComponent* Group = NULL;
    if (!TEST_CHECK_SUCCESS(run, TextComponentFactory_CreateString(factory, (const unsigned char*)u8"Sveiki, \xC4\x81!", &Root)))
    {
        return NULL;
    }
    TextComponent* RootBase = StringComponent_AsComponent(Root);
    RenderColor Gold = { .Tint = { 255, 215, 0, 255 }, .Brightness = 0.7f, .Opacity = 0.3f };
    RenderColor Shadow = { .Tint = { 10, 20, 30, 255 }, .Brightness = 1.0f, .Opacity = 1.0f };
    bool IsBuilt = TEST_CHECK_SUCCESS(run, StringComponent_SetFontName(Root, (const unsigned char*)u8"noto_sans"))
        && TEST_CHECK_SUCCESS(run, StringComponent_SetColor(Root, Gold))
        && TEST_CHECK_SUCCESS(run, StringComponent_SetSize(Root, 0.125f))
        && TEST_CHECK_SUCCESS(run, StringComponent_SetIsShadowActive(Root, false))
        && TEST_CHECK_SUCCESS(run, StringComponent_SetShadowColorCustom(Root, Shadow))
        && TEST_CHECK_SUCCESS(run, StringComponent_SetShadowOffsetCustom(Root, (Vector2){ 0.25f, -0.5f }))
        && TEST_CHECK_SUCCESS(run, StringComponent_SetIsUnderlined(Root, true))
        && TEST_CHECK_SUCCESS(run, StringComponent_SetIsStrikethrough(Root, true))
        && TEST_CHECK_SUCCESS(run, StringComponent_SetSpacing(Root, 1.0f / 3.0f))
        && TEST_CHECK_SUCCESS(run, TextComponentFactory_CreateTranslate(factory, (const unsigned char*)u8"menu.title",
            (const unsigned char*)u8"Title", &Title))
        && TEST_CHECK_SUCCESS(run, StringComponent_SetSize(TranslateComponent_AsString(Title), 0.1f))
        && TEST_CHECK_SUCCESS(run, TextComponent_AddSubComponent(RootBase, TranslateComponent_AsComponent(Title)))
        && TEST_CHECK_SUCCESS(run, TextComponentFactory_CreateEmpty(factory, &Group))
        && TEST_CHECK_SUCCESS(run, TextComponent_AddSubComponent(RootBase, EmptyComponent_AsComponent(Group)))
        && TEST_CHECK_SUCCESS(run, TextComponentFactory_CreateSprite(factory, (const unsigned char*)u8"coin_spin", &Coin))
        && TEST_CHECK_SUCCESS(run, SpriteComponent_SetColor(Coin, Gold))
        && TEST_CHECK_SUCCESS(run, SpriteComponent_SetSize(Coin, (Vector2){ 0.1f, 2.0f / 3.0f }))
        && TEST_CHECK_SUCCESS(run, TextComponent_AddSubComponent(EmptyComponent_AsComponent(Group), SpriteComponent_AsComponent(Coin)))
        && (TestText_AddString(run, factory, EmptyComponent_AsComponent(Group), "plain") != NULL);
    if (!IsBuilt)
    {
        TextComponent* Strays[] =
        {
            (Title != NULL) ? TranslateComponent_AsComponent(Title) : NULL,
            (Group != NULL) ? EmptyComponent_AsComponent(Group) : NULL,
            (Coin != NULL) ? SpriteComponent_AsComponent(Coin) : NULL
        };
        for (size_t Index = 0; Index < (sizeof(Strays) / sizeof(Strays[0])); Index++)
        {
            if ((Strays[Index] != NULL) && (TextComponent_GetParent(Strays[Index]) == NULL))
            {
                TEST_CHECK_SUCCESS(run, TextComponentFactory_ReturnTree(factory, Strays[Index]));
            }
        }
        TEST_CHECK_SUCCESS(run, TextComponentFactory_ReturnTree(factory, RootBase));
        return NULL;
    }
    return RootBase;
}

static void ReturnTree(TestRun* run, TextComponentFactory* factory, TextComponent* root)
{
    if (root != NULL)
    {
        TEST_CHECK_SUCCESS(run, TextComponentFactory_ReturnTree(factory, root));
    }
}

// Serializes a JSON value tree to compact text; the caller frees *outText's data.
static bool SerializeToText(TestRun* run, JSONObjectValue* value, GenericBuffer* outText)
{
    GenericBuffer_AllocateVariable(outText, 0U, sizeof(unsigned char));
    return TEST_CHECK_SUCCESS(run, JSON_Serialize(value, outText, JSONSerializeFlags_NullTerminated));
}

// Deserializes JSON text and expects the codec to fail with InvalidJSON naming every fragment.
static void CheckJSONFailure(TestRun* run, const char* json, const char* const* fragments)
{
    TextComponentFactory Factory;
    JSONObjectPool* Pool = NULL;
    if (!TEST_CHECK_SUCCESS(run, TextComponentFactory_Construct(&Factory)) || !TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool)))
    {
        return;
    }
    JSONObjectValue Value;
    if (TestText_ParseJSON(run, Pool, json, &Value))
    {
        TextComponent* Root = NULL;
        TestText_CheckErrorMessage(run, TextComponentJSON_Deserialize(&Value, &Factory, &Root), ErrorCode_InvalidJSON, fragments);
        TEST_CHECK(run, Root == NULL);
        TEST_CHECK(run, TextComponentFactory_GetLiveComponentCount(&Factory) == 0U);
        TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Value));
    }
    TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
    TEST_CHECK_SUCCESS(run, TextComponentFactory_Deconstruct(&Factory));
}

// Builds "[[[ ... ]]]" with levelCount nested arrays.
static void BuildNestedArrays(GenericBuffer* text, size_t levelCount)
{
    GenericBuffer_AllocateVariable(text, 2U * levelCount + 1U, sizeof(unsigned char));
    for (size_t Index = 0; Index < levelCount; Index++)
    {
        GenericBuffer_AppendByte(text, (unsigned char)'[');
    }
    for (size_t Index = 0; Index < levelCount; Index++)
    {
        GenericBuffer_AppendByte(text, (unsigned char)']');
    }
}


// Tests: JSON.
static void JSONRoundTripKeepsEveryField(TestRun* run)
{
    TextComponentFactory Factory;
    JSONObjectPool* Pool = NULL;
    if (!TEST_CHECK_SUCCESS(run, TextComponentFactory_Construct(&Factory)) || !TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool)))
    {
        return;
    }
    TextComponent* Source = BuildRichTree(run, &Factory);
    JSONObjectValue Value = JSONObjectValue_CreateNull();
    TextComponent* Decoded = NULL;
    if ((Source != NULL) && TEST_CHECK_SUCCESS(run, TextComponentJSON_Serialize(Source, Pool, &Value)))
    {
        // Through text as well, so number formatting is part of the round trip.
        GenericBuffer Text;
        JSONObjectValue Reparsed = JSONObjectValue_CreateNull();
        if (SerializeToText(run, &Value, &Text) && TestText_ParseJSON(run, Pool, (const char*)Text._data, &Reparsed)
            && TEST_CHECK_SUCCESS(run, TextComponentJSON_Deserialize(&Reparsed, &Factory, &Decoded)))
        {
            TEST_CHECK(run, TestText_AreTreesEqual(Source, Decoded));
            // A float is written as its shortest decimal: 0.1f as 0.1, not as the double 0.10000000149011612.
            TEST_CHECK(run, strstr((const char*)Text._data, "\"size\":0.1") != NULL);
            TEST_CHECK(run, strstr((const char*)Text._data, "0.1000") == NULL);
        }
        Memory_Free(Text._data);
        TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Reparsed));
        TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Value));
    }
    ReturnTree(run, &Factory, Decoded);
    ReturnTree(run, &Factory, Source);
    TEST_CHECK(run, TextComponentFactory_GetLiveComponentCount(&Factory) == 0U);
    TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
    TEST_CHECK_SUCCESS(run, TextComponentFactory_Deconstruct(&Factory));
}

static void JSONWritesShorthandsAndOnlyNonDefaults(TestRun* run)
{
    TextComponentFactory Factory;
    JSONObjectPool* Pool = NULL;
    if (!TEST_CHECK_SUCCESS(run, TextComponentFactory_Construct(&Factory)) || !TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool)))
    {
        return;
    }
    EmptyComponent* Root = NULL;
    if (TEST_CHECK_SUCCESS(run, TextComponentFactory_CreateEmpty(&Factory, &Root)))
    {
        TextComponent* RootBase = EmptyComponent_AsComponent(Root);
        StringComponent* Sized = TestText_AddString(run, &Factory, RootBase, "big");
        bool IsBuilt = (TestText_AddString(run, &Factory, RootBase, "plain") != NULL) && (Sized != NULL)
            && TEST_CHECK_SUCCESS(run, StringComponent_SetSize(Sized, 0.5f));
        JSONObjectValue Value = JSONObjectValue_CreateNull();
        if (IsBuilt && TEST_CHECK_SUCCESS(run, TextComponentJSON_Serialize(RootBase, Pool, &Value)))
        {
            // [{ "type": "string", "text": "big", "size": 0.5 }, "plain"]: an empty component is its array of children, a
            // default-styled string its bare text, and an object carries only what differs from the defaults.
            JSONObjectValue Plain = JSONObjectValue_CreateNull();
            JSONObjectValue Big = JSONObjectValue_CreateNull();
            JSONObjectValue Size = JSONObjectValue_CreateNull();
            bool IsShaped = TEST_CHECK(run, (Value.Type == JSONValueType_Array) && (JSONArray_GetElementCount(Value.Value.Array) == 2U))
                && TEST_CHECK_SUCCESS(run, JSONArray_Get(Value.Value.Array, 0U, &Big))
                && TEST_CHECK_SUCCESS(run, JSONArray_Get(Value.Value.Array, 1U, &Plain))
                && TEST_CHECK(run, (Plain.Type == JSONValueType_String) && (Big.Type == JSONValueType_Compound));
            if (IsShaped)
            {
                TEST_CHECK(run, JSONCompound_GetEntryCount(Big.Value.Compound) == 3U);
                TEST_CHECK_SUCCESS(run, JSONCompound_Get(Big.Value.Compound, (const unsigned char*)u8"size", &Size));
                TEST_CHECK(run, (Size.Type == JSONValueType_RealNumber) && (Size.Value.RealNumber == 0.5));
            }
            TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Value));
        }
        ReturnTree(run, &Factory, RootBase);
    }
    TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
    TEST_CHECK_SUCCESS(run, TextComponentFactory_Deconstruct(&Factory));
}

static void JSONReadsShorthandsAndIgnoresUnknownKeys(TestRun* run)
{
    TextComponentFactory Factory;
    JSONObjectPool* Pool = NULL;
    if (!TEST_CHECK_SUCCESS(run, TextComponentFactory_Construct(&Factory)) || !TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool)))
    {
        return;
    }
    JSONObjectValue Value;
    TextComponent* Root = NULL;
    const char* Json = "[\"a\", {\"type\":\"translate\",\"key\":\"menu.quit\",\"future_key\":3,"
        "\"shadow\":{\"color\":\"default\",\"offset\":[0.5,0.5]}}, [\"b\"]]";
    if (TestText_ParseJSON(run, Pool, Json, &Value) && TEST_CHECK_SUCCESS(run, TextComponentJSON_Deserialize(&Value, &Factory, &Root)))
    {
        TEST_CHECK(run, TextComponent_GetType(Root) == TextComponentType_Empty);
        TEST_CHECK(run, TextComponent_GetSubComponentCount(Root) == 3U);
        const TranslateComponent* Quit = TextComponent_AsConstTranslateComponent(TextComponent_GetSubComponentAt(Root, 1U));
        TEST_CHECK(run, (Quit != NULL) && (strcmp((const char*)TranslateComponent_GetKey(Quit), "menu.quit") == 0));
        TEST_CHECK(run, (Quit != NULL) && (StringComponent_GetShadowOffset(&Quit->Base).x == 0.5f));
        TEST_CHECK(run, TextComponent_GetType(TextComponent_GetSubComponentAt(Root, 2U)) == TextComponentType_Empty);
        TEST_CHECK(run, TextComponent_GetHeight(Root) == 3U);
        TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Value));
    }
    ReturnTree(run, &Factory, Root);
    TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
    TEST_CHECK_SUCCESS(run, TextComponentFactory_Deconstruct(&Factory));
}

static void JSONErrorsNameTheFieldAndPath(TestRun* run)
{
    static const char* const SizeFragments[] = { "\"/children/1/children/0\"", "\"size\"", NULL };
    CheckJSONFailure(run, "{\"type\":\"empty\",\"children\":[\"a\",{\"type\":\"empty\",\"children\":"
        "[{\"type\":\"string\",\"text\":\"x\",\"size\":true}]}]}", SizeFragments);

    static const char* const ListFragments[] = { "\"/1\"", "\"shadow.offset\"", "within", NULL };
    CheckJSONFailure(run, "[\"a\",{\"type\":\"string\",\"text\":\"x\",\"shadow\":{\"offset\":[50,0]}}]", ListFragments);

    static const char* const RangeFragments[] = { "the root", "\"size\"", "[0, 100]", NULL };
    CheckJSONFailure(run, "{\"type\":\"string\",\"text\":\"x\",\"size\":120}", RangeFragments);

    static const char* const ColorFragments[] = { "\"color\"", "[0, 1]", NULL };
    CheckJSONFailure(run, "{\"type\":\"sprite\",\"color\":{\"tint\":\"#ffffff\",\"brightness\":2}}", ColorFragments);
}

static void JSONRejectsBadTypesNamesAndMissingFields(TestRun* run)
{
    static const char* const TypeFragments[] = { "\"type\" must be", "\"translate\"", NULL };
    CheckJSONFailure(run, "{\"type\":\"bold\",\"text\":\"x\"}", TypeFragments);
    static const char* const NoTypeFragments[] = { "\"type\" is required", NULL };
    CheckJSONFailure(run, "{\"text\":\"x\"}", NoTypeFragments);
    static const char* const TextFragments[] = { "\"text\"", "required", NULL };
    CheckJSONFailure(run, "{\"type\":\"string\"}", TextFragments);
    static const char* const KeyFragments[] = { "\"key\"", "required", NULL };
    CheckJSONFailure(run, "{\"type\":\"translate\",\"fallback\":\"x\"}", KeyFragments);
    static const char* const NameFragments[] = { "\"font\"", "[a-z][a-z0-9_.-]*", NULL };
    CheckJSONFailure(run, "{\"type\":\"string\",\"text\":\"x\",\"font\":\"Fonts/Bold\"}", NameFragments);
    static const char* const NulFragments[] = { "NUL", NULL };
    CheckJSONFailure(run, "{\"type\":\"string\",\"text\":\"a\\u0000b\"}", NulFragments);
    static const char* const ShapeFragments[] = { "object, an array or a string", NULL };
    CheckJSONFailure(run, "[\"a\", 5]", ShapeFragments);
    static const char* const ChildrenFragments[] = { "\"children\" must be an array", NULL };
    CheckJSONFailure(run, "{\"type\":\"empty\",\"children\":\"a\"}", ChildrenFragments);
}

static void JSONDepthIsLimited(TestRun* run)
{
    TextComponentFactory Factory;
    JSONObjectPool* Pool = NULL;
    if (!TEST_CHECK_SUCCESS(run, TextComponentFactory_Construct(&Factory)) || !TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool)))
    {
        return;
    }
    size_t LevelCounts[] = { (size_t)TEXT_COMPONENT_MAX_DEPTH, (size_t)TEXT_COMPONENT_MAX_DEPTH + 1U };
    for (size_t Case = 0; Case < 2U; Case++)
    {
        GenericBuffer Text;
        BuildNestedArrays(&Text, LevelCounts[Case]);
        JSONObjectValue Value;
        TextComponent* Root = NULL;
        if (TEST_CHECK_SUCCESS(run, JSON_Deserialize(Pool, &Text, &Value)))
        {
            Error Result = TextComponentJSON_Deserialize(&Value, &Factory, &Root);
            if (Case == 0U)
            {
                TEST_CHECK_SUCCESS(run, Result);
                TEST_CHECK(run, (Root != NULL) && (TextComponent_GetHeight(Root) == (size_t)TEXT_COMPONENT_MAX_DEPTH));
            }
            else
            {
                static const char* const Fragments[] = { "deeper than 64", NULL };
                TestText_CheckErrorMessage(run, Result, ErrorCode_InvalidJSON, Fragments);
            }
            ReturnTree(run, &Factory, Root);
            TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Value));
        }
        Memory_Free(Text._data);
    }
    TEST_CHECK(run, TextComponentFactory_GetLiveComponentCount(&Factory) == 0U);
    TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
    TEST_CHECK_SUCCESS(run, TextComponentFactory_Deconstruct(&Factory));
}


// Reports whether a component compound stores a shadow offset (GHDF entry 17).
static bool HasGHDFShadowOffset(TestRun* run, GHDFCompound* compound)
{
    GHDFObjectValue Entry;
    bool IsFound = false;
    TEST_CHECK_SUCCESS(run, GHDFCompound_GetOptionalVerified(compound, (GHDFEntryID)17,
        GHDF_CreateRegularType(GHDFValueType_Compound), &Entry, &IsFound));
    return IsFound;
}

// Checks one string component's shadow offset through GHDF: stored only when custom, and read back the same way.
static void CheckGHDFShadowOffset(TestRun* run, TextComponentFactory* factory, TextComponent* component, bool isCustom)
{
    GHDFObjectPool* Pool = NULL;
    if (!TEST_CHECK_SUCCESS(run, GHDFObjectPool_Create(&Pool)))
    {
        return;
    }
    GHDFCompound* Compound = NULL;
    TextComponent* Decoded = NULL;
    if (TEST_CHECK_SUCCESS(run, TextComponentGHDF_Serialize(component, Pool, &Compound)))
    {
        TEST_CHECK(run, HasGHDFShadowOffset(run, Compound) == isCustom);
        if (TEST_CHECK_SUCCESS(run, TextComponentGHDF_Deserialize(Compound, factory, &Decoded)))
        {
            TEST_CHECK(run, TestText_AreTreesEqual(component, Decoded));
            ReturnTree(run, factory, Decoded);
        }
        TEST_CHECK_SUCCESS(run, GHDFObjectPool_ReturnCompound(Pool, Compound, true));
    }
    TEST_CHECK_SUCCESS(run, GHDFObjectPool_Deconstruct(Pool));
}

// A string without its own shadow offset uses its font's. That is the field's absence (or "default"), never a value,
// so a custom offset equal to the usual font default is still custom, and is written.
static void ShadowOffsetIsDefaultOrCustom(TestRun* run)
{
    TextComponentFactory Factory;
    JSONObjectPool* Pool = NULL;
    if (!TEST_CHECK_SUCCESS(run, TextComponentFactory_Construct(&Factory)) || !TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool)))
    {
        return;
    }
    const char* Json = "[\"plain\", {\"type\":\"string\",\"text\":\"a\",\"shadow\":{\"offset\":\"default\"}},"
        "{\"type\":\"string\",\"text\":\"b\",\"shadow\":{\"offset\":[-0.1,0.1]}}]";
    JSONObjectValue Value;
    TextComponent* Root = NULL;
    if (TestText_ParseJSON(run, Pool, Json, &Value) && TEST_CHECK_SUCCESS(run, TextComponentJSON_Deserialize(&Value, &Factory, &Root)))
    {
        const StringComponent* Plain = TextComponent_AsConstStringComponent(TextComponent_GetSubComponentAt(Root, 0U));
        const StringComponent* Default = TextComponent_AsConstStringComponent(TextComponent_GetSubComponentAt(Root, 1U));
        const StringComponent* Custom = TextComponent_AsConstStringComponent(TextComponent_GetSubComponentAt(Root, 2U));
        TEST_CHECK(run, StringComponent_GetShadowOffsetType(Plain) == TextShadowOffsetType_Default);
        TEST_CHECK(run, StringComponent_GetShadowOffsetType(Default) == TextShadowOffsetType_Default);
        TEST_CHECK(run, (StringComponent_GetShadowOffsetType(Custom) == TextShadowOffsetType_Custom)
            && (StringComponent_GetShadowOffset(Custom).x == -0.1f) && (StringComponent_GetShadowOffset(Custom).y == 0.1f));

        // Written back, the default leaves the field out (the second string becomes a bare text) and the custom one
        // keeps it.
        JSONObjectValue Written = JSONObjectValue_CreateNull();
        if (TEST_CHECK_SUCCESS(run, TextComponentJSON_Serialize(Root, Pool, &Written)))
        {
            JSONObjectValue Second = JSONObjectValue_CreateNull();
            JSONObjectValue Third = JSONObjectValue_CreateNull();
            JSONObjectValue Shadow = JSONObjectValue_CreateNull();
            JSONObjectValue Offset = JSONObjectValue_CreateNull();
            bool IsShaped = TEST_CHECK(run, Written.Type == JSONValueType_Array)
                && TEST_CHECK_SUCCESS(run, JSONArray_Get(Written.Value.Array, 1U, &Second))
                && TEST_CHECK_SUCCESS(run, JSONArray_Get(Written.Value.Array, 2U, &Third))
                && TEST_CHECK(run, (Second.Type == JSONValueType_String) && (Third.Type == JSONValueType_Compound))
                && TEST_CHECK_SUCCESS(run, JSONCompound_Get(Third.Value.Compound, (const unsigned char*)u8"shadow", &Shadow))
                && TEST_CHECK(run, Shadow.Type == JSONValueType_Compound);
            if (IsShaped)
            {
                TEST_CHECK_SUCCESS(run, JSONCompound_Get(Shadow.Value.Compound, (const unsigned char*)u8"offset", &Offset));
                TEST_CHECK(run, Offset.Type == JSONValueType_Array);
            }
            TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Written));
        }

        CheckGHDFShadowOffset(run, &Factory, TextComponent_GetSubComponentAt(Root, 1U), false);
        CheckGHDFShadowOffset(run, &Factory, TextComponent_GetSubComponentAt(Root, 2U), true);
        TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Value));
    }
    ReturnTree(run, &Factory, Root);

    static const char* const RangeFragments[] = { "\"shadow.offset\"", "\"default\" or two decimals", NULL };
    CheckJSONFailure(run, "{\"type\":\"string\",\"text\":\"x\",\"shadow\":{\"offset\":[0,11]}}", RangeFragments);
    static const char* const ShapeFragments[] = { "\"shadow.offset\"", NULL };
    CheckJSONFailure(run, "{\"type\":\"string\",\"text\":\"x\",\"shadow\":{\"offset\":\"auto\"}}", ShapeFragments);

    TEST_CHECK(run, TextComponentFactory_GetLiveComponentCount(&Factory) == 0U);
    TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
    TEST_CHECK_SUCCESS(run, TextComponentFactory_Deconstruct(&Factory));
}

// Tests: GHDF.
static void GHDFRoundTripKeepsEveryField(TestRun* run)
{
    TextComponentFactory Factory;
    GHDFObjectPool* Pool = NULL;
    if (!TEST_CHECK_SUCCESS(run, TextComponentFactory_Construct(&Factory)) || !TEST_CHECK_SUCCESS(run, GHDFObjectPool_Create(&Pool)))
    {
        return;
    }
    TextComponent* Source = BuildRichTree(run, &Factory);
    GHDFCompound* Compound = NULL;
    TextComponent* InMemory = NULL;
    TextComponent* FromBytes = NULL;
    if ((Source != NULL) && TEST_CHECK_SUCCESS(run, TextComponentGHDF_Serialize(Source, Pool, &Compound)))
    {
        // The type is written as an explicit wire id (2 = string), not as the enum's value.
        GHDFObjectValue TypeEntry;
        if (TEST_CHECK_SUCCESS(run, GHDFCompound_GetVerified(Compound, (GHDFEntryID)1,
            GHDF_CreateRegularType(GHDFValueType_UInt8), &TypeEntry)))
        {
            TEST_CHECK(run, TypeEntry.Value.UInt8 == 2U);
        }
        if (TEST_CHECK_SUCCESS(run, TextComponentGHDF_Deserialize(Compound, &Factory, &InMemory)))
        {
            TEST_CHECK(run, TestText_AreTreesEqual(Source, InMemory));
        }

        MemoryStream Stream;
        GHDFCompound* ReadBack = NULL;
        if (TEST_CHECK_SUCCESS(run, MemoryStream_Construct1(&Stream, (IOStreamFlags)(IOStreamFlags_CanRead | IOStreamFlags_CanWrite))))
        {
            IOStream* StreamInterface = MemoryStream_AsIOStream(&Stream);
            if (TEST_CHECK_SUCCESS(run, GHDF_Write(Compound, StreamInterface))
                && TEST_CHECK_SUCCESS(run, IOStream_SetPosition(StreamInterface, 0U))
                && TEST_CHECK_SUCCESS(run, GHDF_Read(StreamInterface, Pool, &ReadBack))
                && TEST_CHECK_SUCCESS(run, TextComponentGHDF_Deserialize(ReadBack, &Factory, &FromBytes)))
            {
                TEST_CHECK(run, TestText_AreTreesEqual(Source, FromBytes));
            }
            if (ReadBack != NULL)
            {
                TEST_CHECK_SUCCESS(run, GHDFObjectPool_ReturnCompound(Pool, ReadBack, true));
            }
            MemoryStream_Deconstruct(&Stream);
        }
        TEST_CHECK_SUCCESS(run, GHDFObjectPool_ReturnCompound(Pool, Compound, true));
    }
    ReturnTree(run, &Factory, FromBytes);
    ReturnTree(run, &Factory, InMemory);
    ReturnTree(run, &Factory, Source);
    TEST_CHECK(run, TextComponentFactory_GetLiveComponentCount(&Factory) == 0U);
    TEST_CHECK_SUCCESS(run, GHDFObjectPool_Deconstruct(Pool));
    TEST_CHECK_SUCCESS(run, TextComponentFactory_Deconstruct(&Factory));
}

// Builds a one-component compound with the given type id; extra entries are added by the caller.
static GHDFCompound* BuildComponentCompound(TestRun* run, GHDFObjectPool* pool, uint8_t typeId)
{
    GHDFCompound* Compound = NULL;
    if (!TEST_CHECK_SUCCESS(run, GHDFObjectPool_BorrowCompound(pool, &Compound)))
    {
        return NULL;
    }
    GHDFObjectValue Type = GHDFObjectValue_CreateUInt8(typeId);
    if (!TEST_CHECK_SUCCESS(run, GHDFCompound_SetValue(Compound, (GHDFEntryID)1, GHDF_CreateRegularType(GHDFValueType_UInt8), &Type)))
    {
        TEST_CHECK_SUCCESS(run, GHDFObjectPool_ReturnCompound(pool, Compound, true));
        return NULL;
    }
    return Compound;
}

static void SetGHDFString(TestRun* run, GHDFObjectPool* pool, GHDFCompound* compound, GHDFEntryID id, const unsigned char* bytes,
    size_t byteCount)
{
    GenericBuffer* Buffer = NULL;
    if (TEST_CHECK_SUCCESS(run, GHDFObjectPool_BorrowString(pool, &Buffer)))
    {
        TEST_CHECK(run, GenericBuffer_AppendRangeBytes(Buffer, (unsigned char*)bytes, byteCount));
        TEST_CHECK(run, GenericBuffer_NullTerminate(Buffer));
        GHDFObjectValue Value = GHDFObjectValue_CreateString(Buffer);
        if (!TEST_CHECK_SUCCESS(run, GHDFCompound_SetValue(compound, id, GHDF_CreateRegularType(GHDFValueType_String), &Value)))
        {
            TEST_CHECK_SUCCESS(run, GHDFObjectPool_ReturnString(pool, Buffer));
        }
    }
}

static void GHDFRejectsBadValues(TestRun* run)
{
    TextComponentFactory Factory;
    GHDFObjectPool* Pool = NULL;
    if (!TEST_CHECK_SUCCESS(run, TextComponentFactory_Construct(&Factory)) || !TEST_CHECK_SUCCESS(run, GHDFObjectPool_Create(&Pool)))
    {
        return;
    }
    TextComponent* Root = NULL;

    GHDFCompound* UnknownType = BuildComponentCompound(run, Pool, 9U);
    if (UnknownType != NULL)
    {
        static const char* const Fragments[] = { "type id 9", NULL };
        TestText_CheckErrorMessage(run, TextComponentGHDF_Deserialize(UnknownType, &Factory, &Root), ErrorCode_Deserialize, Fragments);
        TEST_CHECK_SUCCESS(run, GHDFObjectPool_ReturnCompound(Pool, UnknownType, true));
    }

    GHDFCompound* WrongSizeType = BuildComponentCompound(run, Pool, 2U);
    if (WrongSizeType != NULL)
    {
        SetGHDFString(run, Pool, WrongSizeType, (GHDFEntryID)10, (const unsigned char*)u8"x", 1U);
        GHDFObjectValue Size = GHDFObjectValue_CreateUInt8(3U);
        TEST_CHECK_SUCCESS(run, GHDFCompound_SetValue(WrongSizeType, (GHDFEntryID)13, GHDF_CreateRegularType(GHDFValueType_UInt8), &Size));
        static const char* const Fragments[] = { "\"size\"", NULL };
        TestText_CheckErrorMessage(run, TextComponentGHDF_Deserialize(WrongSizeType, &Factory, &Root), ErrorCode_Deserialize, Fragments);
        TEST_CHECK_SUCCESS(run, GHDFObjectPool_ReturnCompound(Pool, WrongSizeType, true));
    }

    GHDFCompound* BadText = BuildComponentCompound(run, Pool, 2U);
    if (BadText != NULL)
    {
        const unsigned char Invalid[] = { 0xC3, 0x28 };
        SetGHDFString(run, Pool, BadText, (GHDFEntryID)10, Invalid, sizeof(Invalid));
        static const char* const Fragments[] = { "\"text\"", "UTF-8", NULL };
        TestText_CheckErrorMessage(run, TextComponentGHDF_Deserialize(BadText, &Factory, &Root), ErrorCode_Deserialize, Fragments);
        TEST_CHECK_SUCCESS(run, GHDFObjectPool_ReturnCompound(Pool, BadText, true));
    }

    TEST_CHECK(run, Root == NULL);
    TEST_CHECK(run, TextComponentFactory_GetLiveComponentCount(&Factory) == 0U);
    TEST_CHECK_SUCCESS(run, GHDFObjectPool_Deconstruct(Pool));
    TEST_CHECK_SUCCESS(run, TextComponentFactory_Deconstruct(&Factory));
}


// Public functions.
void TextComponentCodecTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "json_round_trip_keeps_every_field", JSONRoundTripKeepsEveryField },
        { "json_writes_shorthands_and_only_non_defaults", JSONWritesShorthandsAndOnlyNonDefaults },
        { "json_reads_shorthands_and_ignores_unknown_keys", JSONReadsShorthandsAndIgnoresUnknownKeys },
        { "json_errors_name_the_field_and_path", JSONErrorsNameTheFieldAndPath },
        { "json_rejects_bad_types_names_and_missing_fields", JSONRejectsBadTypesNamesAndMissingFields },
        { "json_depth_is_limited", JSONDepthIsLimited },
        { "shadow_offset_is_default_or_custom", ShadowOffsetIsDefaultOrCustom },
        { "ghdf_round_trip_keeps_every_field", GHDFRoundTripKeepsEveryField },
        { "ghdf_rejects_bad_values", GHDFRejectsBadValues },
    };
    TestRun_RunSuite(run, "TextComponentCodec", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
