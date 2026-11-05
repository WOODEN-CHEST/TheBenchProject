#include "TestSuites.h"
#include "TestAssets.h"
#include "TestWorld.h"
#include "AssetManager.h"
#include "DataValue.h"
#include "Prefab.h"
#include "World.h"
#include "WorldFile.h"
#include "WorldLightComponent.h"
#include "WorldObject.h"
#include "WorldPrefabComponent.h"
#include "WorldPrefabs.h"
#include "wr/WRFileSystem.h"
#include "wr/WRJSON.h"
#include "wr/WRMemory.h"
#include "wr/WRRandom.h"
#include "wr/WRString.h"
#include <math.h>
#include <stdio.h>
#include <string.h>


// Macros.
/** A prefab's objects most cases share: a root, a tagged roof, a lamp with a light, a nested copy's root. */
#define OBJECTS "\"objects\": [ { \"id\": 1, \"name\": \"root\" }, " \
    "{ \"id\": 2, \"parent\": 1, \"name\": \"roof\", \"tags\": [\"tbp:roof\"] }, " \
    "{ \"id\": 3, \"parent\": 1, \"name\": \"lamp\", \"tags\": [\"tbp:lit\"], \"components\": { \"light\": { \"range\": 2 } } }, " \
    "{ \"id\": 4, \"parent\": 1, \"name\": \"inner\", \"components\": { \"prefab\": { } } } ]"
/** A file of a property block over those objects. */
#define WITH_PROPERTY(block) "{ \"name\": \"p\", \"format_version\": 3, \"properties\": { \"thing\": " block " }, " OBJECTS " }"
#define NEAR(a, b, tolerance) (fabs((double)(a) - (double)(b)) <= (double)(tolerance))


// Types.
/* A file a definition must refuse, and a fragment of why. */
typedef struct RefusalStruct
{
    const char* File;
    const char* Fragment;
} Refusal;


// Fields.
static const Refusal FileRefusals[] =
{
    { "{ \"name\": \"p\", \"format_version\": 3, \"objectz\": [] }", "objectz" },
    { "{ \"name\": \"p\", " OBJECTS " }", "format_version" },
    { "{ \"name\": \"p\", \"format_version\": 999, " OBJECTS " }", "newer" },
    { "{ \"name\": \"p\", \"format_version\": 0, " OBJECTS " }", "at least 1" },
    { "{ \"name\": \"p\", \"format_version\": 3 }", "at least one object" },
    { "{ \"name\": \"p\", \"format_version\": 3, \"objects\": [] }", "at least one object" },
    { "{ \"name\": \"p\", \"format_version\": 3, \"objects\": [ { \"id\": 1, \"name\": \"a\" }, { \"id\": 2, \"name\": \"b\" } ] }", "one root" },
    { "{ \"name\": \"p\", \"format_version\": 3, \"objects\": [ { \"id\": 1 } ] }", "needs a name" },
    { "{ \"name\": \"p\", \"format_version\": 3, \"objects\": [ { \"id\": 1, \"name\": \"Big Name\" } ] }", "needs a name" },
    { "{ \"name\": \"p\", \"format_version\": 3, \"objects\": [ { \"id\": 1, \"name\": \"a/b\" } ] }", "needs a name" },
    { "{ \"name\": \"p\", \"format_version\": 3, \"objects\": [ { \"id\": 1, \"name\": \"a\" }, "
        "{ \"id\": 2, \"parent\": 1, \"name\": \"a\" } ] }", "two objects are named \"a\"" },
    { "{ \"name\": \"p\", \"format_version\": 3, \"objects\": [ { \"id\": 1, \"name\": \"a\", \"positon\": [0, 0, 0] } ] }", "positon" },
    { "{ \"name\": \"p\", \"format_version\": 3, \"objects\": [ { \"id\": 1, \"name\": \"a\", \"components\": { \"gizmo\": { } } } ] }", "gizmo" },
    { "{ \"name\": \"p\", \"format_version\": 3, \"objects\": [ { \"id\": 1, \"name\": \"a\", \"components\": { \"light\": { \"rangee\": 1 } } } ] }", "rangee" },
    { "{ \"name\": \"p\", \"format_version\": 3, \"objects\": [ { \"id\": 1, \"name\": \"a\", \"position\": [1, 2] } ] }", "world_object.position" },
    { "{ \"name\": \"p\", \"format_version\": 3, \"objects\": [ { \"id\": 1, \"name\": \"a\", \"parent\": 9 } ] }", "no object has that id" },
    { "{ \"name\": \"p\", \"format_version\": 3, \"objects\": [ { \"id\": 1, \"name\": \"a\", \"parent\": 2 }, { \"id\": 2, \"name\": \"b\", \"parent\": 1 } ] }", "cycle" },
    { "{ \"name\": \"p\", \"format_version\": 3, \"objects\": [ { \"name\": \"a\" } ] }", "no \"id\"" },
    { "{ \"name\": \"p\", \"format_version\": 3, \"properties\": 3, " OBJECTS " }", "\"properties\" must be" },
    { "{ \"name\": \"p\", \"format_version\": 3, \"properties\": { \"thing\": 3 }, " OBJECTS " }", "must be an object" },
    { "{ \"name\": \"p\", \"format_version\": 3, \"properties\": { \"Big Thing\": { \"default\": 1 } }, " OBJECTS " }", "local name" },
};

static const Refusal PropertyRefusals[] =
{
    { WITH_PROPERTY("{ \"type\": \"float\" }"), "needs a \"default\"" },
    { WITH_PROPERTY("{ \"type\": \"fuzzy\", \"default\": 1 }"), "prefab_property.type" },
    { WITH_PROPERTY("{ \"default\": 1 }"), "needs a \"type\"" },
    { WITH_PROPERTY("{ \"type\": \"enum\", \"default\": \"a\", \"options\": { \"a\": [] }, \"option_display_names\": { \"b\": \"B\" } }"), "which is no option" },
    { WITH_PROPERTY("{ \"type\": \"enum\", \"default\": \"a\", \"options\": { \"a\": [] }, \"option_display_names\": { \"a\": \"A\", \"b\": \"B\" } }"), "more than there are options" },
    { WITH_PROPERTY("{ \"type\": \"float\", \"default\": 1, \"option_display_names\": { \"a\": \"A\" } }"), "only an enum takes" },
    { WITH_PROPERTY("{ \"type\": \"float\", \"default\": 1, \"size\": 2 }"), "size" },
    { WITH_PROPERTY("{ \"type\": \"float\", \"default\": \"no\" }"), "must be a number" },
    { WITH_PROPERTY("{ \"type\": \"bool\", \"default\": 1 }"), "true or false" },
    { WITH_PROPERTY("{ \"type\": \"int\", \"default\": 1.5 }"), "whole number" },
    { WITH_PROPERTY("{ \"type\": \"color\", \"default\": \"green\" }"), "must be a colour" },
    { WITH_PROPERTY("{ \"type\": \"vector\", \"default\": [1, 2] }"), "list of 3 numbers" },
    { WITH_PROPERTY("{ \"type\": \"vector2\", \"default\": [1, 2, 3] }"), "list of 2 numbers" },
    { WITH_PROPERTY("{ \"type\": \"vector\", \"default\": [\"a\", \"b\", \"c\"] }"), "list of 3 numbers" },
    { WITH_PROPERTY("{ \"type\": \"asset\", \"default\": \"Not An Asset\" }"), "asset's name" },
    { WITH_PROPERTY("{ \"type\": \"string\", \"default\": 3 }"), "must be a text" },
    { WITH_PROPERTY("{ \"type\": \"float\", \"default\": 9, \"maximum\": 5 }"), "outside its range" },
    { WITH_PROPERTY("{ \"type\": \"float\", \"default\": 1, \"minimum\": 5 }"), "outside its range" },
    { WITH_PROPERTY("{ \"type\": \"vector\", \"default\": [1, 2, 9], \"maximum\": 5 }"), "outside its range" },
    { WITH_PROPERTY("{ \"type\": \"float\", \"default\": 1, \"minimum\": 5, \"maximum\": 2 }"), "minimum lies above" },
    { WITH_PROPERTY("{ \"type\": \"bool\", \"default\": true, \"minimum\": 0 }"), "takes a range" },
    { WITH_PROPERTY("{ \"type\": \"float\", \"default\": 1, \"asset_type\": \"model\" }"), "only an asset" },
    { WITH_PROPERTY("{ \"type\": \"asset\", \"default\": \"x\", \"asset_type\": \"nothing_like_it\" }"), "no asset type" },
    { WITH_PROPERTY("{ \"type\": \"enum\", \"default\": \"a\" }"), "needs \"options\"" },
    { WITH_PROPERTY("{ \"type\": \"enum\", \"default\": \"a\", \"options\": { } }"), "needs \"options\"" },
    { WITH_PROPERTY("{ \"type\": \"enum\", \"default\": \"b\", \"options\": { \"a\": [] } }"), "one of its options" },
    { WITH_PROPERTY("{ \"type\": \"enum\", \"default\": 1, \"options\": { \"a\": [] } }"), "one of its options" },
    { WITH_PROPERTY("{ \"type\": \"enum\", \"default\": \"a\", \"options\": { \"a\": [] }, \"targets\": [] }"), "takes no \"targets\"" },
    { WITH_PROPERTY("{ \"type\": \"enum\", \"default\": \"a\", \"options\": { \"Big\": [] } }"), "local name" },
    { WITH_PROPERTY("{ \"type\": \"enum\", \"default\": \"a\", \"options\": { \"a\": 3 } }"), "array of records" },
    { WITH_PROPERTY("{ \"type\": \"enum\", \"default\": \"a\", \"options\": { \"a\": [] }, \"option_display_names\": 3 }"), "option_display_names" },
    { WITH_PROPERTY("{ \"type\": \"enum\", \"default\": \"a\", \"options\": { \"a\": [] }, \"option_display_names\": { \"a\": 3 } }"), "display name" },
    { WITH_PROPERTY("{ \"type\": \"float\", \"default\": 1, \"options\": { } }"), "only an enum" },
    { WITH_PROPERTY("{ \"type\": \"float\", \"default\": 1, \"targets\": [ { \"object\": \"roof\", \"tag\": \"tbp:roof\", \"row\": \"scale\" } ] }"), "exactly one of \"object\"" },
    { WITH_PROPERTY("{ \"type\": \"float\", \"default\": 1, \"targets\": [ { \"row\": \"scale\" } ] }"), "exactly one of \"object\"" },
    { WITH_PROPERTY("{ \"type\": \"float\", \"default\": 1, \"targets\": [ { \"object\": \"roof\" } ] }"), "exactly one of \"row\"" },
    { WITH_PROPERTY("{ \"type\": \"float\", \"default\": 1, \"targets\": [ { \"object\": \"inner\", \"row\": \"scale\", \"property\": \"x\" } ] }"), "exactly one of \"row\"" },
    { WITH_PROPERTY("{ \"type\": \"float\", \"default\": 1, \"targets\": [ { \"object\": \"attic\", \"row\": \"scale\", \"axis\": \"x\" } ] }"), "no object is named \"attic\"" },
    { WITH_PROPERTY("{ \"type\": \"float\", \"default\": 1, \"targets\": [ { \"object\": \"roof\", \"row\": \"scalee\" } ] }"), "no row \"scalee\"" },
    { WITH_PROPERTY("{ \"type\": \"float\", \"default\": 1, \"targets\": [ { \"object\": \"roof\", \"component\": \"gizmo\", \"row\": \"x\" } ] }"), "no component type" },
    { WITH_PROPERTY("{ \"type\": \"float\", \"default\": 1, \"targets\": [ { \"object\": \"roof\", \"component\": \"light\", \"row\": \"range\" } ] }"), "has no such component" },
    { WITH_PROPERTY("{ \"type\": \"float\", \"default\": 1, \"targets\": [ { \"tag\": \"tbp:attic\", \"row\": \"scale\", \"axis\": \"x\" } ] }"), "no object has this tag" },
    { WITH_PROPERTY("{ \"type\": \"float\", \"default\": 1, \"targets\": [ { \"tag\": \"tbp:roof\", \"component\": \"light\", \"row\": \"range\" } ] }"), "no object has this tag" },
    { WITH_PROPERTY("{ \"type\": \"float\", \"default\": 1, \"targets\": [ { \"tag\": \"Not A Key\", \"row\": \"scale\" } ] }"), "the tag" },
    { WITH_PROPERTY("{ \"type\": \"int\", \"default\": 1, \"targets\": [ { \"object\": \"roof\", \"row\": \"parent\" } ] }"), "cannot be written" },
    { WITH_PROPERTY("{ \"type\": \"string\", \"default\": \"x\", \"targets\": [ { \"object\": \"roof\", \"row\": \"name\" } ] }"), "cannot be written" },
    { WITH_PROPERTY("{ \"type\": \"string\", \"default\": \"x\", \"targets\": [ { \"object\": \"roof\", \"row\": \"data\" } ] }"), "cannot be written" },
    { WITH_PROPERTY("{ \"type\": \"float\", \"default\": 1, \"targets\": [ { \"object\": \"roof\", \"row\": \"is_enabled\" } ] }"), "row refuses the value" },
    { WITH_PROPERTY("{ \"type\": \"float\", \"default\": 1, \"targets\": [ { \"object\": \"lamp\", \"component\": \"light\", \"row\": \"color\" } ] }"), "row refuses the value" },
    { WITH_PROPERTY("{ \"type\": \"float\", \"default\": -1, \"targets\": [ { \"object\": \"lamp\", \"component\": \"light\", \"row\": \"range\" } ] }"), "row refuses the value" },
    { WITH_PROPERTY("{ \"type\": \"float\", \"default\": 1, \"targets\": [ { \"object\": \"roof\", \"row\": \"tint\", \"axis\": \"x\" } ] }"), "no such axis" },
    { WITH_PROPERTY("{ \"type\": \"vector\", \"default\": [1, 2, 3], \"targets\": [ { \"object\": \"roof\", \"row\": \"scale\", \"axis\": \"x\" } ] }"), "an axis takes a number" },
    { WITH_PROPERTY("{ \"type\": \"float\", \"default\": 1, \"targets\": [ { \"object\": \"roof\", \"property\": \"x\" } ] }"), "has no such component" },
    { WITH_PROPERTY("{ \"type\": \"float\", \"default\": 1, \"targets\": [ { \"object\": \"inner\", \"component\": \"prefab\", \"property\": \"x\" } ] }"), "no \"component\"" },
    { WITH_PROPERTY("{ \"type\": \"float\", \"default\": 1, \"targets\": [ { \"object\": \"inner\", \"axis\": \"x\", \"property\": \"x\" } ] }"), "no \"axis\"" },
    { WITH_PROPERTY("{ \"type\": \"float\", \"default\": 1, \"targets\": [ { \"object\": \"roof\", \"row\": \"scale\", \"axis\": \"x\", \"value\": 2 } ] }"), "only an enum option's" },
    { WITH_PROPERTY("{ \"type\": \"float\", \"default\": 1, \"targets\": [ { \"object\": \"roof\", \"row\": \"scale\", \"axis\": \"w\" } ] }"), "prefab_target.axis" },
    { WITH_PROPERTY("{ \"type\": \"float\", \"default\": 1, \"targets\": [ { \"object\": \"roof\", \"row\": \"scale\", \"axiss\": \"x\" } ] }"), "axiss" },
    { WITH_PROPERTY("{ \"type\": \"enum\", \"default\": \"a\", \"options\": { \"a\": [ { \"object\": \"roof\", \"row\": \"is_enabled\" } ] } }"), "needs a \"value\"" },
    { WITH_PROPERTY("{ \"type\": \"enum\", \"default\": \"a\", \"options\": { \"a\": [ { \"object\": \"roof\", \"row\": \"is_enabled\", \"value\": 3 } ] } }"), "row refuses the value" },
    { WITH_PROPERTY("{ \"type\": \"enum\", \"default\": \"a\", \"options\": { \"a\": [ { \"object\": \"attic\", \"row\": \"is_enabled\", \"value\": false } ] } }"), "no object is named" },
};


// Static functions.
static Error BuildDefinition(TestAssetFixture* fixture, const char* json, AssetDefinition** outDefinition)
{
    GenericBuffer Raw;
    GenericBuffer_AllocateVariable(&Raw, 0U, sizeof(unsigned char));
    GenericBuffer_AppendString(&Raw, (const unsigned char*)json);
    Error Result = AssetManager_BuildDefinition(fixture->Manager, fixture->Types.Prefab, &Raw, (const unsigned char*)u8"test prefab", outDefinition);
    Memory_Free(Raw._data);
    return Result;
}

static void CheckRefusal(TestRun* run, TestAssetFixture* fixture, const Refusal* refusal)
{
    AssetDefinition* Definition = NULL;
    Error Result = BuildDefinition(fixture, refusal->File, &Definition);
    TEST_CHECK(run, Result.Code != ErrorCode_Success);
    if (Result.Code == ErrorCode_Success)
    {
        printf("        accepted: %s\n", refusal->File);
        Definition->VTable->Destroy(Definition);
        return;
    }
    bool HasFragment = (Result.Message != NULL) && (strstr((const char*)Result.Message, refusal->Fragment) != NULL);
    TEST_CHECK(run, HasFragment);
    TEST_CHECK(run, Result.Code == ErrorCode_InvalidAssetDefinition);
    if (!HasFragment || (Result.Code != ErrorCode_InvalidAssetDefinition))
    {
        printf("        wanted \"%s\" in (%d): %s\n", refusal->Fragment, (int)Result.Code, (Result.Message != NULL) ? (const char*)Result.Message : "");
    }
    Error_Deconstruct(&Result);
}

static bool AddPrefab(TestRun* run, TestAssetFixture* fixture, const char* json)
{
    AssetDefinition* Definition = NULL;
    return TEST_CHECK_SUCCESS(run, BuildDefinition(fixture, json, &Definition))
        && TEST_CHECK_SUCCESS(run, AssetManager_SetDefinition(fixture->Manager, Definition));
}


// Tests.
static void EveryRuleOfAFileIsRefusedClearly(TestRun* run)
{
    TestAssetFixture Fixture;
    if (TestAssetFixture_Construct(run, &Fixture))
    {
        for (size_t Index = 0U; Index < (sizeof(FileRefusals) / sizeof(FileRefusals[0])); Index++)
        {
            CheckRefusal(run, &Fixture, &FileRefusals[Index]);
        }
        TestAssetFixture_Deconstruct(run, &Fixture);
    }
}

static void EveryRuleOfAPropertyIsRefusedClearly(TestRun* run)
{
    TestAssetFixture Fixture;
    if (TestAssetFixture_Construct(run, &Fixture))
    {
        for (size_t Index = 0U; Index < (sizeof(PropertyRefusals) / sizeof(PropertyRefusals[0])); Index++)
        {
            CheckRefusal(run, &Fixture, &PropertyRefusals[Index]);
        }
        TestAssetFixture_Deconstruct(run, &Fixture);
    }
}

/* An axis leaves the rest of its row as the template has it, so a rule over the whole row judges it with those, not with the zeros of an empty
 * row. */
static void AnAxisIsJudgedOverItsTemplatesRow(TestRun* run)
{
    TestAssetFixture Fixture;
    if (!TestAssetFixture_Construct(run, &Fixture))
    {
        return;
    }
    TEST_CHECK(run, AddPrefab(run, &Fixture, "{ \"name\": \"ruled\", \"format_version\": 3, \"properties\": { \"tall\": { \"type\": \"float\", \"default\": 2,"
        " \"targets\": [ { \"object\": \"box\", \"component\": \"collider\", \"row\": \"scale\", \"axis\": \"y\" },"
        " { \"object\": \"box\", \"component\": \"collider\", \"row\": \"shape_plane_half_extents\", \"axis\": \"x\" } ] } },"
        " \"objects\": [ { \"id\": 1, \"name\": \"box\", \"components\": { \"collider\": { } } } ] }"));
    const Refusal Zero = { "{ \"name\": \"ruled\", \"format_version\": 3, \"properties\": { \"tall\": { \"type\": \"float\", \"default\": 0,"
        " \"targets\": [ { \"object\": \"box\", \"component\": \"collider\", \"row\": \"scale\", \"axis\": \"y\" } ] } },"
        " \"objects\": [ { \"id\": 1, \"name\": \"box\", \"components\": { \"collider\": { } } } ] }", "refuses the value" };
    CheckRefusal(run, &Fixture, &Zero);
    TestAssetFixture_Deconstruct(run, &Fixture);
}

static void AnAssetsDeclarationsAreWhatTheFileSays(TestRun* run)
{
    TestAssetFixture Fixture;
    if (!TestAssetFixture_Construct(run, &Fixture))
    {
        return;
    }
    bool IsAdded = AddPrefab(run, &Fixture,
        "{ \"name\": \"mod:stop\", \"format_version\": 3, \"properties\": {"
        "  \"roof_color\": { \"type\": \"color\", \"default\": \"#7a3b2e\", \"display_name\": \"Roof colour\", \"category\": \"Looks\","
        "      \"description\": \"The colour of its roof.\", \"targets\": [ { \"tag\": \"tbp:roof\", \"row\": \"tint\" } ] },"
        "  \"layout\": { \"type\": \"enum\", \"default\": \"b\", \"options\": { \"a\": [], \"b\": [ { \"tag\": \"tbp:roof\", \"row\": \"is_enabled\", \"value\": false },"
        "      { \"object\": \"inner\", \"property\": \"size\", \"value\": 3 } ] }, \"option_display_names\": { \"b\": \"The second\" } },"
        "  \"count\": { \"type\": \"int\", \"default\": 2, \"minimum\": 0, \"maximum\": 9 },"
        "  \"model\": { \"type\": \"asset\", \"default\": \"mod:thing\", \"asset_type\": \"world_model\", "
        "      \"targets\": [ { \"object\": \"inner\", \"component\": \"prefab\", \"row\": \"prefab\" } ] }"
        "  }, " OBJECTS " }");
    Prefab* Asset = NULL;
    AssetUserID User = ASSET_USER_ID_INVALID;
    TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(Fixture.Manager, &User));
    if (IsAdded && TEST_CHECK_SUCCESS(run, AssetManager_LoadPrefab(Fixture.Manager, (const unsigned char*)u8"mod:stop", User, &Asset)))
    {
        TEST_CHECK(run, Prefab_GetObjectCount(Asset) == 4U);
        TEST_CHECK(run, strcmp((const char*)Prefab_GetObjectName(Asset, 0U), "root") == 0);
        TEST_CHECK(run, Prefab_GetObjectParent(Asset, 0U) == PREFAB_NO_PARENT);
        uint32_t Index = 99U;
        TEST_CHECK(run, Prefab_TryFindObject(Asset, (const unsigned char*)u8"lampshade", 4U, &Index));
        TEST_CHECK(run, strcmp((const char*)Prefab_GetObjectName(Asset, Index), "lamp") == 0);
        TEST_CHECK(run, Prefab_GetObjectParent(Asset, Index) == 0U);
        TEST_CHECK(run, !Prefab_TryFindObject(Asset, (const unsigned char*)u8"lampshade", 5U, &Index));
        TEST_CHECK(run, !Prefab_TryFindObject(Asset, (const unsigned char*)u8"", 0U, &Index));
        TEST_CHECK(run, Prefab_GetPropertyCount(Asset) == 4U);

        const PrefabProperty* Roof = Prefab_GetProperty(Asset, 0U);
        TEST_CHECK(run, strcmp((const char*)Roof->Name, "roof_color") == 0); // the order of the file
        TEST_CHECK(run, Roof->Type == PrefabPropertyType_Color);
        TEST_CHECK(run, strcmp((const char*)Roof->DisplayName, "Roof colour") == 0);
        TEST_CHECK(run, strcmp((const char*)Roof->Category, "Looks") == 0);
        TEST_CHECK(run, strcmp((const char*)Roof->Description, "The colour of its roof.") == 0);
        TEST_CHECK(run, (Roof->TargetCount == 1U) && (Roof->Targets[0].ObjectCount == 1U));
        TEST_CHECK(run, Roof->Targets[0].ComponentType == PREFAB_NO_COMPONENT);
        TEST_CHECK(run, !Roof->HasMinimum && !Roof->HasMaximum);

        const PrefabProperty* Layout = Prefab_FindProperty(Asset, (const unsigned char*)u8"layout");
        TEST_CHECK(run, (Layout != NULL) && (Layout->OptionCount == 2U) && (Layout->TargetCount == 0U));
        const PrefabOption* Second = PrefabProperty_FindOption(Layout, (const unsigned char*)u8"b");
        TEST_CHECK(run, (Second != NULL) && (Second->TargetCount == 2U) && (strcmp((const char*)Second->DisplayName, "The second") == 0));
        TEST_CHECK(run, PrefabProperty_FindOption(Layout, (const unsigned char*)u8"a")->DisplayName == NULL);
        TEST_CHECK(run, PrefabProperty_FindOption(Layout, (const unsigned char*)u8"c") == NULL);
        TEST_CHECK(run, Second->Targets[1].Row == NULL);
        TEST_CHECK(run, Second->Targets[1].ComponentType == (int32_t)WorldComponentType_Prefab);

        const PrefabProperty* Count = Prefab_FindProperty(Asset, (const unsigned char*)u8"count");
        TEST_CHECK(run, Count->HasMinimum && Count->HasMaximum && NEAR(Count->Maximum, 9.0, 1e-12) && (Count->Default->IntValue == 2));
        TEST_CHECK(run, Prefab_FindProperty(Asset, (const unsigned char*)u8"nothing") == NULL);
        TEST_CHECK(run, strcmp((const char*)Prefab_FindProperty(Asset, (const unsigned char*)u8"model")->AssetType, "world_model") == 0);
    }
    if (User != ASSET_USER_ID_INVALID)
    {
        TEST_CHECK_SUCCESS(run, AssetManager_RetireUser(Fixture.Manager, User));
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

static void AValueIsCheckedByItsPropertysType(TestRun* run)
{
    TestAssetFixture Fixture;
    if (!TestAssetFixture_Construct(run, &Fixture))
    {
        return;
    }
    bool IsAdded = AddPrefab(run, &Fixture,
        "{ \"name\": \"kinds\", \"format_version\": 3, \"properties\": {"
        "  \"b\": { \"type\": \"bool\", \"default\": false }, \"i\": { \"type\": \"int\", \"default\": 1, \"minimum\": 0, \"maximum\": 3 },"
        "  \"f\": { \"type\": \"float\", \"default\": 1, \"minimum\": 0.5, \"maximum\": 3 }, \"c\": { \"type\": \"color\", \"default\": \"#ffffff\" },"
        "  \"v\": { \"type\": \"vector\", \"default\": [0, 0, 0], \"maximum\": 3 }, \"w\": { \"type\": \"vector2\", \"default\": [0, 0] },"
        "  \"a\": { \"type\": \"angle\", \"default\": 0, \"maximum\": 90 },"
        "  \"e\": { \"type\": \"enum\", \"default\": \"x\", \"options\": { \"x\": [], \"y\": [] } },"
        "  \"n\": { \"type\": \"asset\", \"default\": \"desk\" }, \"s\": { \"type\": \"string\", \"default\": \"\" }"
        "  }, \"objects\": [ { \"id\": 1, \"name\": \"root\" } ] }");
    Prefab* Asset = NULL;
    AssetUserID User = ASSET_USER_ID_INVALID;
    JSONObjectPool* Pool = NULL;
    TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(Fixture.Manager, &User));
    TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool));
    if (IsAdded && (Pool != NULL) && TEST_CHECK_SUCCESS(run, AssetManager_LoadPrefab(Fixture.Manager, (const unsigned char*)u8"kinds", User, &Asset)))
    {
        DataValue Text = DataValue_CreateTag();
        DataValue Green = DataValue_CreateTag();
        DataValue BadName = DataValue_CreateTag();
        DataValue Two = DataValue_CreateTag();
        DataValue Three = DataValue_CreateTag();
        DataValue Far = DataValue_CreateTag();
        TEST_CHECK_SUCCESS(run, DataValue_CreateString((const unsigned char*)u8"y", &Text));
        TEST_CHECK_SUCCESS(run, DataValue_CreateString((const unsigned char*)u8"#00ff00", &Green));
        TEST_CHECK_SUCCESS(run, DataValue_CreateString((const unsigned char*)u8"Not An Asset", &BadName));
        TEST_CHECK_SUCCESS(run, DataValue_CreateList(DataType_Float, &Two));
        TEST_CHECK_SUCCESS(run, DataValue_CreateList(DataType_Float, &Three));
        TEST_CHECK_SUCCESS(run, DataValue_CreateList(DataType_Float, &Far));
        for (size_t Index = 0U; Index < 3U; Index++)
        {
            DataValue One = DataValue_FromFloat(1.0);
            DataValue Big = DataValue_FromFloat((Index == 2U) ? 9.0 : 1.0);
            TEST_CHECK_SUCCESS(run, DataValue_ListAdd(&Three, &One));
            TEST_CHECK_SUCCESS(run, DataValue_ListAdd(&Far, &Big));
        }
        DataValue One = DataValue_FromFloat(1.0);
        DataValue AlsoOne = DataValue_FromFloat(1.0);
        TEST_CHECK_SUCCESS(run, DataValue_ListAdd(&Two, &One));
        TEST_CHECK_SUCCESS(run, DataValue_ListAdd(&Two, &AlsoOne));

        const struct { const char* Property; DataValue Value; bool IsGood; } Cases[] =
        {
            { "b", DataValue_FromBool(true), true }, { "b", DataValue_FromInt(1), false },
            { "i", DataValue_FromInt(3), true }, { "i", DataValue_FromInt(4), false }, { "i", DataValue_FromInt(-1), false },
            { "i", DataValue_FromFloat(1.0), false },
            { "f", DataValue_FromInt(2), true }, { "f", DataValue_FromFloat(0.5), true }, { "f", DataValue_FromFloat(0.25), false },
            { "f", DataValue_FromBool(true), false },
            { "a", DataValue_FromFloat(90.0), true }, { "a", DataValue_FromFloat(90.5), false },
            { "c", DataValue_FromColor((Color){ 1, 2, 3, 255 }), true }, { "c", DataValue_FromInt(3), false },
            { "v", DataValue_FromVector3(Vector3Double_Create(1.0, 2.0, 3.0)), true },
            { "v", DataValue_FromVector3(Vector3Double_Create(1.0, 2.0, 4.0)), false },
        };
        for (size_t Index = 0U; Index < (sizeof(Cases) / sizeof(Cases[0])); Index++)
        {
            Error Result = PrefabProperty_CheckValue(Prefab_FindProperty(Asset, (const unsigned char*)Cases[Index].Property), &Cases[Index].Value, Pool);
            TEST_CHECK(run, (Result.Code == ErrorCode_Success) == Cases[Index].IsGood);
            Error_Deconstruct(&Result);
        }
#define CHECK_VALUE(name, value, code) \
        TEST_CHECK_ERROR(run, PrefabProperty_CheckValue(Prefab_FindProperty(Asset, (const unsigned char*)name), value, Pool), code)
        CHECK_VALUE("e", &Text, ErrorCode_Success);
        CHECK_VALUE("e", &Green, ErrorCode_ArgumentOutOfRange);
        CHECK_VALUE("c", &Green, ErrorCode_Success);
        CHECK_VALUE("c", &Text, ErrorCode_ArgumentOutOfRange);
        CHECK_VALUE("n", &Text, ErrorCode_Success);
        CHECK_VALUE("n", &BadName, ErrorCode_ArgumentOutOfRange);
        CHECK_VALUE("s", &BadName, ErrorCode_Success);
        CHECK_VALUE("v", &Three, ErrorCode_Success);
        CHECK_VALUE("v", &Far, ErrorCode_ArgumentOutOfRange);
        CHECK_VALUE("v", &Two, ErrorCode_ArgumentOutOfRange);
        CHECK_VALUE("w", &Two, ErrorCode_Success);
        CHECK_VALUE("w", &Three, ErrorCode_ArgumentOutOfRange);
#undef CHECK_VALUE
        DataValue_Deconstruct(&Text);
        DataValue_Deconstruct(&Green);
        DataValue_Deconstruct(&BadName);
        DataValue_Deconstruct(&Two);
        DataValue_Deconstruct(&Three);
        DataValue_Deconstruct(&Far);
    }
    if (Pool != NULL)
    {
        TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
    }
    if (User != ASSET_USER_ID_INVALID)
    {
        TEST_CHECK_SUCCESS(run, AssetManager_RetireUser(Fixture.Manager, User));
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

/* Lists of more than a file may hold are refused before anything is built of them. */
static void AFilesListsHaveLimits(TestRun* run)
{
    TestAssetFixture Fixture;
    if (!TestAssetFixture_Construct(run, &Fixture))
    {
        return;
    }
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, 0U, sizeof(unsigned char));
    // Too many objects.
    GenericBuffer_AppendString(&Text, (const unsigned char*)u8"{ \"name\": \"p\", \"format_version\": 3, \"objects\": [ { \"id\": 1, \"name\": \"root\" }");
    for (size_t Index = 0U; Index < PREFAB_MAX_OBJECTS; Index++)
    {
        char Part[96];
        snprintf(Part, sizeof(Part), ", { \"id\": %zu, \"parent\": 1, \"name\": \"o%zu\" }", Index + 2U, Index);
        GenericBuffer_AppendString(&Text, (const unsigned char*)Part);
    }
    GenericBuffer_AppendString(&Text, (const unsigned char*)u8" ] }");
    GenericBuffer_NullTerminate(&Text);
    Refusal TooMany = { (const char*)Text._data, "at most 16384" };
    CheckRefusal(run, &Fixture, &TooMany);
    GenericBuffer_Clear(&Text);
    // Too many properties.
    GenericBuffer_AppendString(&Text, (const unsigned char*)u8"{ \"name\": \"p\", \"format_version\": 3, \"properties\": {");
    for (size_t Index = 0U; Index <= PREFAB_MAX_PROPERTIES; Index++)
    {
        char Part[96];
        snprintf(Part, sizeof(Part), "%s \"p%zu\": { \"type\": \"float\", \"default\": 1 }", (Index == 0U) ? "" : ",", Index);
        GenericBuffer_AppendString(&Text, (const unsigned char*)Part);
    }
    GenericBuffer_AppendString(&Text, (const unsigned char*)u8" }, " OBJECTS " }");
    GenericBuffer_NullTerminate(&Text);
    Refusal TooManyProperties = { (const char*)Text._data, "at most 256" };
    CheckRefusal(run, &Fixture, &TooManyProperties);
    GenericBuffer_Clear(&Text);
    // Tags that reach too many objects: small in the file, large in memory.
    GenericBuffer_AppendString(&Text, (const unsigned char*)u8"{ \"name\": \"p\", \"format_version\": 3, \"properties\": {");
    for (size_t Property = 0U; Property < 70U; Property++)
    {
        char Part[96];
        snprintf(Part, sizeof(Part), "%s \"p%zu\": { \"type\": \"float\", \"default\": 1, \"targets\": [", (Property == 0U) ? "" : ",", Property);
        GenericBuffer_AppendString(&Text, (const unsigned char*)Part);
        for (size_t Target = 0U; Target < 1000U; Target++)
        {
            GenericBuffer_AppendString(&Text, (const unsigned char*)((Target == 0U) ? u8"{ \"tag\": \"tbp:a\", \"row\": \"scale\", \"axis\": \"x\" }"
                : u8", { \"tag\": \"tbp:a\", \"row\": \"scale\", \"axis\": \"x\" }"));
        }
        GenericBuffer_AppendString(&Text, (const unsigned char*)u8"] }");
    }
    GenericBuffer_AppendString(&Text, (const unsigned char*)u8" }, \"objects\": [ { \"id\": 1, \"name\": \"root\", \"tags\": [\"tbp:a\"] } ] }");
    GenericBuffer_NullTerminate(&Text);
    Refusal TooFar = { (const char*)Text._data, "objects a prefab's may" };
    CheckRefusal(run, &Fixture, &TooFar);
    GenericBuffer_Clear(&Text);
    // Tags that each look through many objects, and reach one: a small file's long wait.
    GenericBuffer_AppendString(&Text, (const unsigned char*)u8"{ \"name\": \"p\", \"format_version\": 3, \"properties\": {");
    for (size_t Property = 0U; Property < 3U; Property++)
    {
        char Part[96];
        snprintf(Part, sizeof(Part), "%s \"p%zu\": { \"type\": \"float\", \"default\": 1, \"targets\": [", (Property == 0U) ? "" : ",", Property);
        GenericBuffer_AppendString(&Text, (const unsigned char*)Part);
        for (size_t Target = 0U; Target < 1000U; Target++)
        {
            GenericBuffer_AppendString(&Text, (const unsigned char*)((Target == 0U) ? u8"{ \"tag\": \"tbp:a\", \"row\": \"scale\", \"axis\": \"x\" }"
                : u8", { \"tag\": \"tbp:a\", \"row\": \"scale\", \"axis\": \"x\" }"));
        }
        GenericBuffer_AppendString(&Text, (const unsigned char*)u8"] }");
    }
    GenericBuffer_AppendString(&Text, (const unsigned char*)u8" }, \"objects\": [ { \"id\": 1, \"name\": \"root\", \"tags\": [\"tbp:a\"] }");
    for (size_t Index = 0U; Index < 1400U; Index++)
    {
        char Part[96];
        snprintf(Part, sizeof(Part), ", { \"id\": %zu, \"parent\": 1, \"name\": \"o%zu\" }", Index + 2U, Index);
        GenericBuffer_AppendString(&Text, (const unsigned char*)Part);
    }
    GenericBuffer_AppendString(&Text, (const unsigned char*)u8" ] }");
    GenericBuffer_NullTerminate(&Text);
    Refusal TooLong = { (const char*)Text._data, "look through more than" };
    CheckRefusal(run, &Fixture, &TooLong);
    Memory_Free(Text._data);
    TestAssetFixture_Deconstruct(run, &Fixture);
}

/* A hostile file: any byte of a good one changed. It must be refused or work, and whatever works must make copies safely, under the
 * sanitizers too. */
static void AFileWithAnyByteChangedIsRefusedOrWorks(TestRun* run)
{
    const char* const Good =
        "{ \"name\": \"fuzz\", \"format_version\": 3, \"properties\": {"
        "  \"roof\": { \"type\": \"color\", \"default\": \"#112233\", \"targets\": [ { \"tag\": \"tbp:roof\", \"row\": \"tint\" } ] },"
        "  \"layout\": { \"type\": \"enum\", \"default\": \"a\", \"options\": { \"a\": [ { \"object\": \"door\", \"row\": \"is_enabled\", \"value\": false } ], \"b\": [] } },"
        "  \"angle\": { \"type\": \"angle\", \"default\": 10, \"minimum\": 0, \"maximum\": 90, \"targets\": [ { \"object\": \"door\", \"row\": \"rotation\", \"axis\": \"y\" } ] },"
        "  \"range\": { \"type\": \"float\", \"default\": 2, \"targets\": [ { \"object\": \"lamp\", \"component\": \"light\", \"row\": \"range\" } ] },"
        "  \"pass\": { \"type\": \"int\", \"default\": 2, \"targets\": [ { \"object\": \"inner\", \"property\": \"pass\" } ] }"
        "  }, \"objects\": [ { \"id\": 1, \"name\": \"root\" },"
        "  { \"id\": 2, \"parent\": 1, \"name\": \"roof\", \"tags\": [\"tbp:roof\"], \"position\": [0, 3, 0], \"data\": { \"town:n\": { \"$key\": \"town:o\" } } },"
        "  { \"id\": 3, \"parent\": 2, \"name\": \"door\" },"
        "  { \"id\": 4, \"parent\": 1, \"name\": \"lamp\", \"components\": { \"light\": { \"range\": 2 } } },"
        "  { \"id\": 5, \"parent\": 1, \"name\": \"inner\", \"components\": { \"prefab\": { \"prefab\": \"fuzz\", \"values\": { \"pass\": 3 },"
        "    \"object_overrides\": { \"door\": { \"is_removed\": true }, \"inner/lamp\": { \"scale\": [2, 2, 2] } } } } }"
        "  ] }";
    size_t Length = strlen(Good);
    unsigned char* Text = Memory_Allocate(Length + 1U);
    Random Generator;
    Random_Construct2(&Generator, (RandomState){ .Values = { 0xB11U, 0x7U, 0x3U, 0x1U } });
    TestAssetFixture Fixture;
    if (TestAssetFixture_Construct(run, &Fixture))
    {
        const char Replacements[] = "01299aaezz-#._";
        size_t Accepted = 0U;
        size_t Made = 0U;
        for (size_t Round = 0U; Round < 1500U; Round++)
        {
            Memory_Copy(Good, Text, Length + 1U);
            // One change mostly, or little that is changed gets past the JSON and reaches anything further in.
            size_t Changes = (Round == 0U) ? 0U : (((Round % 8U) == 0U) ? 3U : 1U);
            for (size_t Change = 0U; Change < Changes; Change++)
            {
                size_t At = (size_t)Random_NextInt32InLimit(&Generator, (int32_t)Length);
                Text[At] = (unsigned char)Replacements[Random_NextInt32InLimit(&Generator, (int32_t)(sizeof(Replacements) - 1U))];
            }
            AssetDefinition* Definition = NULL;
            Error Result = BuildDefinition(&Fixture, (const char*)Text, &Definition);
            if (Round == 0U)
            {
                TEST_CHECK(run, Result.Code == ErrorCode_Success); // the file itself is good
                if (Result.Code != ErrorCode_Success)
                {
                    printf("        %s\n", (Result.Message != NULL) ? (const char*)Result.Message : "");
                }
            }
            if (Result.Code == ErrorCode_Success)
            {
                Accepted++;
                Result = AssetManager_SetDefinition(Fixture.Manager, Definition);
                if (Result.Code == ErrorCode_Success)
                {
                    World TheWorld;
                    KeyRegistry Keys;
                    if (TestWorld_Construct(run, &TheWorld, &Keys, Fixture.Manager, NULL))
                    {
                        WorldPrefabs* System = NULL;
                        WorldObject* Root = NULL;
                        WorldComponent* Component = NULL;
                        if (TEST_CHECK_SUCCESS(run, WorldPrefabs_Create(&TheWorld, Fixture.Manager, NULL, &System))
                            && TEST_CHECK_SUCCESS(run, World_CreateObject(&TheWorld, NULL, &Root))
                            && TEST_CHECK_SUCCESS(run, World_AddComponent(&TheWorld, WorldObject_GetId(Root), WorldComponentType_Prefab, &Component)))
                        {
                            // A name a changed byte may have made another's is simply one that does not load.
                            TEST_CHECK_SUCCESS(run, WorldPrefabComponent_SetPrefab(WorldPrefabComponent_FromComponent(Component), (const unsigned char*)u8"fuzz"));
                            WorldPrefabs_Update(System);
                            Made += WorldPrefabs_GetStats(System).ObjectCount;
                        }
                        WorldPrefabs_Destroy(System);
                        TestWorld_Deconstruct(run, &TheWorld, &Keys);
                    }
                }
            }
            Error_Deconstruct(&Result);
        }
        printf("        %zu files of 1500 accepted, %zu objects made\n", Accepted, Made);
        TEST_CHECK(run, Accepted > 50U);
        TEST_CHECK(run, Made > 100U);
        TestAssetFixture_Deconstruct(run, &Fixture);
    }
    Random_Deconstruct1(&Generator);
    Memory_Free(Text);
}


/* The shipped street: the prefabs load from their files, and the test world's copies come to what they say. */
static void TheShippedStreetIsWhatItsFilesSay(TestRun* run)
{
    AssetManager* Manager = NULL;
    JSONObjectPool* Pool = NULL;
    StandardAssetTypes Types;
    GenericBuffer Root;
    GenericBuffer_AllocateVariable(&Root, 0U, sizeof(unsigned char));
    if (!TEST_CHECK_SUCCESS(run, AssetManager_Construct1(NULL, &Manager)) || !TEST_CHECK_SUCCESS(run, AssetManager_CreateStandardAssetTypes(Manager, &Types, &Pool))
        || !TEST_CHECK_SUCCESS(run, TestRun_BuildRepositoryPath(run, "compile/out/asset", &Root))
        || !TEST_CHECK_SUCCESS(run, AssetManager_AddSearchRoot(Manager, Root._data)))
    {
        Memory_Free(Root._data);
        return;
    }
    size_t Failed = 0U;
    TEST_CHECK_SUCCESS(run, AssetManager_ReadDefinitions(Manager, &Failed));
    TEST_CHECK(run, Failed == 0U);
    KeyRegistry Keys;
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, 0U, sizeof(unsigned char));
    TEST_CHECK_SUCCESS(run, KeyRegistry_Construct(&Keys));
    World Loaded;
    WorldFileReadOptions Options = WorldFileReadOptions_Create(&Keys, NULL, NULL);
    if (TEST_CHECK_SUCCESS(run, TestRun_BuildRepositoryPath(run, "compile/out/worlds/test.world.json", &Path))
        && TEST_CHECK_SUCCESS(run, WorldFile_LoadJSON(Path._data, &Options, &Loaded)))
    {
        WorldPrefabs* System = NULL;
        if (TEST_CHECK_SUCCESS(run, WorldPrefabs_Create(&Loaded, Manager, NULL, &System)))
        {
            size_t ObjectsBefore = World_GetObjectCount(&Loaded);
            uint64_t NextId = World_GetNextObjectId(&Loaded);
            WorldPrefabs_Update(System);
            TEST_CHECK(run, WorldPrefabs_IsSettled(System));
            WorldPrefabsStats Stats = WorldPrefabs_GetStats(System);
            // Three stops with a lamp each; three houses of 69 objects and 40 bays of 4; street lamps are the path objects' to make.
            size_t StopObjects = 9U + 2U;
            size_t HouseObjects = 69U + (40U * 4U);
            TEST_CHECK(run, Stats.ObjectCount == ((3U * StopObjects) - 1U + (3U * HouseObjects))); // one stop removed its bin
            TEST_CHECK(run, Stats.CopyCount == (3U + 3U + 3U + (3U * 40U)));
            TEST_CHECK(run, World_GetObjectCount(&Loaded) == (ObjectsBefore + Stats.ObjectCount));
            TEST_CHECK(run, World_GetNextObjectId(&Loaded) == NextId);

            uint64_t Id = 0U;
            WorldObject* Object = NULL;
            // The changed stop: its colour, its layout, its bench, no bin, a row through its lamp, its own child.
            TEST_CHECK(run, WorldPrefabs_TryFindObject(System, 112U, (const unsigned char*)u8"roof", &Id) && World_TryGetObject(&Loaded, Id, &Object)
                && (WorldObject_GetTint(Object).Tint.r == 0x2E));
            TEST_CHECK(run, WorldPrefabs_TryFindObject(System, 112U, (const unsigned char*)u8"left_side", &Id) && World_TryGetObject(&Loaded, Id, &Object)
                && WorldObject_IsEnabled(Object));
            TEST_CHECK(run, !WorldPrefabs_TryFindObject(System, 112U, (const unsigned char*)u8"bin", &Id));
            TEST_CHECK(run, WorldPrefabs_TryFindObject(System, 112U, (const unsigned char*)u8"lamp/light", &Id) && World_TryGetObject(&Loaded, Id, &Object)
                && NEAR(WorldLightComponent_FromComponent(WorldObject_GetComponent(Object, WorldComponentType_Light))->_intensity, 6.0, 1e-6));
            // The plain stop's defaults.
            TEST_CHECK(run, WorldPrefabs_TryFindObject(System, 111U, (const unsigned char*)u8"left_side", &Id) && World_TryGetObject(&Loaded, Id, &Object)
                && !WorldObject_IsEnabled(Object));
            // A house's values through two levels: the wall colour on a bay's wall, the front's option on a bay's kind, the chimney's axis.
            TEST_CHECK(run, WorldPrefabs_TryFindObject(System, 116U, (const unsigned char*)u8"bay_0_0_0/plain", &Id) && World_TryGetObject(&Loaded, Id, &Object)
                && (WorldObject_GetTint(Object).Tint.r == 0xB8));
            TEST_CHECK(run, WorldPrefabs_TryFindObject(System, 116U, (const unsigned char*)u8"bay_0_0_1/with_door", &Id) && World_TryGetObject(&Loaded, Id, &Object)
                && WorldObject_IsEnabled(Object));
            TEST_CHECK(run, WorldPrefabs_TryFindObject(System, 115U, (const unsigned char*)u8"bay_0_0_1/with_door", &Id) && World_TryGetObject(&Loaded, Id, &Object)
                && !WorldObject_IsEnabled(Object));
            TEST_CHECK(run, WorldPrefabs_TryFindObject(System, 116U, (const unsigned char*)u8"chimney", &Id) && World_TryGetObject(&Loaded, Id, &Object)
                && NEAR(WorldObject_GetScale(Object).y, 2.5, 1e-6) && NEAR(WorldObject_GetScale(Object).x, 1.0, 1e-6));
            TEST_CHECK(run, WorldPrefabs_TryFindObject(System, 117U, (const unsigned char*)u8"chimney", &Id) && World_TryGetObject(&Loaded, Id, &Object)
                && !WorldObject_IsEnabled(Object));
            // Written back, the world holds what it held: nothing a copy made reached the file.
            GenericBuffer Written;
            GenericBuffer Shipped;
            GenericBuffer_AllocateVariable(&Written, 0U, sizeof(unsigned char));
            GenericBuffer_AllocateVariable(&Shipped, 0U, sizeof(unsigned char));
            TEST_CHECK_SUCCESS(run, WorldFile_WriteJSON(&Loaded, &Written));
            TEST_CHECK_SUCCESS(run, FileSystem_ReadAllBytes(Path._data, &Shipped));
            TEST_CHECK(run, (Written._count == Shipped._count) && Memory_IsEqual(Written._data, Shipped._data, Written._count));
            Memory_Free(Written._data);
            Memory_Free(Shipped._data);
            WorldPrefabs_Destroy(System);
            TEST_CHECK(run, World_GetObjectCount(&Loaded) == ObjectsBefore);
        }
        TEST_CHECK_SUCCESS(run, World_Deconstruct(&Loaded));
    }
    KeyRegistry_Deconstruct(&Keys);
    Memory_Free(Path._data);
    Memory_Free(Root._data);
    TEST_CHECK_SUCCESS(run, AssetManager_Deconstruct(Manager));
    TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
}


void PrefabTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "every_rule_of_a_file_is_refused_clearly", EveryRuleOfAFileIsRefusedClearly },
        { "every_rule_of_a_property_is_refused_clearly", EveryRuleOfAPropertyIsRefusedClearly },
        { "an_assets_declarations_are_what_the_file_says", AnAssetsDeclarationsAreWhatTheFileSays },
        { "a_value_is_checked_by_its_propertys_type", AValueIsCheckedByItsPropertysType },
        { "a_files_lists_have_limits", AFilesListsHaveLimits },
        { "an_axis_is_judged_over_its_templates_row", AnAxisIsJudgedOverItsTemplatesRow },
        { "a_file_with_any_byte_changed_is_refused_or_works", AFileWithAnyByteChangedIsRefusedOrWorks },
        { "the_shipped_street_is_what_its_files_say", TheShippedStreetIsWhatItsFilesSay },
    };
    TestRun_RunSuite(run, "Prefab", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
