#include "TestText.h"
#include <stdio.h>
#include <string.h>


// Static functions.
static bool AreColorsEqual(RenderColor a, RenderColor b)
{
    return (a.Tint.r == b.Tint.r) && (a.Tint.g == b.Tint.g) && (a.Tint.b == b.Tint.b) && (a.Brightness == b.Brightness)
        && (a.Opacity == b.Opacity);
}

static bool AreVectorsEqual(Vector2 a, Vector2 b)
{
    return (a.x == b.x) && (a.y == b.y);
}

static bool AreNamesEqual(const unsigned char* a, const unsigned char* b)
{
    if ((a == NULL) || (b == NULL))
    {
        return a == b;
    }
    return strcmp((const char*)a, (const char*)b) == 0;
}

static bool AreStringPartsEqual(const StringComponent* a, const StringComponent* b)
{
    bool AreShadowOffsetsEqual = (StringComponent_GetShadowOffsetType(a) == StringComponent_GetShadowOffsetType(b))
        && ((StringComponent_GetShadowOffsetType(a) == TextShadowOffsetType_Default)
            || AreVectorsEqual(StringComponent_GetShadowOffset(a), StringComponent_GetShadowOffset(b)));
    bool AreShadowColorsEqual = (StringComponent_GetShadowColorType(a) == StringComponent_GetShadowColorType(b))
        && ((StringComponent_GetShadowColorType(a) == TextShadowColorType_Default)
            || AreColorsEqual(StringComponent_GetShadowColor(a), StringComponent_GetShadowColor(b)));
    return AreNamesEqual(StringComponent_GetText(a), StringComponent_GetText(b))
        && AreNamesEqual(StringComponent_GetFontName(a), StringComponent_GetFontName(b))
        && AreColorsEqual(StringComponent_GetColor(a), StringComponent_GetColor(b))
        && (StringComponent_GetSize(a) == StringComponent_GetSize(b))
        && (StringComponent_IsShadowActive(a) == StringComponent_IsShadowActive(b))
        && AreShadowColorsEqual
        && AreShadowOffsetsEqual
        && (StringComponent_IsUnderlined(a) == StringComponent_IsUnderlined(b))
        && (StringComponent_IsStrikethrough(a) == StringComponent_IsStrikethrough(b))
        && (StringComponent_GetSpacing(a) == StringComponent_GetSpacing(b));
}

static bool AreOwnPropertiesEqual(const TextComponent* a, const TextComponent* b)
{
    if (TextComponent_GetType(a) != TextComponent_GetType(b))
    {
        return false;
    }
    switch (TextComponent_GetType(a))
    {
        case TextComponentType_Empty:
            return true;
        case TextComponentType_String:
            return AreStringPartsEqual(TextComponent_AsConstStringComponent(a), TextComponent_AsConstStringComponent(b));
        case TextComponentType_Translate:
            return AreNamesEqual(TranslateComponent_GetKey(TextComponent_AsConstTranslateComponent(a)),
                    TranslateComponent_GetKey(TextComponent_AsConstTranslateComponent(b)))
                && AreStringPartsEqual(TextComponent_AsConstStringComponent(a), TextComponent_AsConstStringComponent(b));
        case TextComponentType_Sprite:
        {
            const SpriteComponent* SpriteA = TextComponent_AsConstSpriteComponent(a);
            const SpriteComponent* SpriteB = TextComponent_AsConstSpriteComponent(b);
            return AreNamesEqual(SpriteComponent_GetAnimationName(SpriteA), SpriteComponent_GetAnimationName(SpriteB))
                && AreColorsEqual(SpriteComponent_GetColor(SpriteA), SpriteComponent_GetColor(SpriteB))
                && AreVectorsEqual(SpriteComponent_GetSize(SpriteA), SpriteComponent_GetSize(SpriteB));
        }
    }
    return false;
}


// Public functions.
bool TestText_ParseJSON(TestRun* run, JSONObjectPool* pool, const char* text, JSONObjectValue* outValue)
{
    GenericBuffer Source;
    GenericBuffer_AllocateVariable(&Source, 0U, sizeof(unsigned char));
    bool IsAppended = TEST_CHECK(run, GenericBuffer_AppendString(&Source, (const unsigned char*)text));
    bool IsParsed = IsAppended && TEST_CHECK_SUCCESS(run, JSON_Deserialize(pool, &Source, outValue));
    Memory_Free(Source._data);
    return IsParsed;
}

bool TestText_AreTreesEqual(const TextComponent* a, const TextComponent* b)
{
    if (!AreOwnPropertiesEqual(a, b) || (TextComponent_GetSubComponentCount(a) != TextComponent_GetSubComponentCount(b)))
    {
        return false;
    }
    for (size_t Index = 0; Index < TextComponent_GetSubComponentCount(a); Index++)
    {
        if (!TestText_AreTreesEqual(TextComponent_GetSubComponentAt(a, Index), TextComponent_GetSubComponentAt(b, Index)))
        {
            return false;
        }
    }
    return true;
}

bool TestText_CheckErrorMessage(TestRun* run, Error result, ErrorCode expectedCode, const char* const* fragments)
{
    bool IsCodeExpected = TEST_CHECK(run, result.Code == expectedCode);
    bool AreFragmentsFound = true;
    const char* Message = (result.Message != NULL) ? (const char*)result.Message : "";
    for (size_t Index = 0; fragments[Index] != NULL; Index++)
    {
        if (strstr(Message, fragments[Index]) == NULL)
        {
            printf("      message \"%s\" lacks \"%s\"\n", Message, fragments[Index]);
            AreFragmentsFound = false;
        }
    }
    TEST_CHECK(run, AreFragmentsFound);
    Error_Deconstruct(&result);
    return IsCodeExpected && AreFragmentsFound;
}

StringComponent* TestText_AddString(TestRun* run, TextComponentFactory* factory, TextComponent* parent, const char* text)
{
    StringComponent* Child = NULL;
    if (!TEST_CHECK_SUCCESS(run, TextComponentFactory_CreateString(factory, (const unsigned char*)text, &Child)))
    {
        return NULL;
    }
    if (!TEST_CHECK_SUCCESS(run, TextComponent_AddSubComponent(parent, StringComponent_AsComponent(Child))))
    {
        TEST_CHECK_SUCCESS(run, TextComponentFactory_ReturnTree(factory, StringComponent_AsComponent(Child)));
        return NULL;
    }
    return Child;
}
