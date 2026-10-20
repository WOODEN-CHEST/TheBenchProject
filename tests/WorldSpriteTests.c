#include "TestSuites.h"
#include "TestAssets.h"
#include "TestText.h"
#include "TestWorld.h"
#include "World.h"
#include "WorldObject.h"
#include "WorldSprite.h"
#include "WorldSpriteComponent.h"
#include "wr/WRFileSystem.h"
#include "wr/WRJSON.h"
#include "wr/WRString.h"
#include <math.h>
#include <stdio.h>
#include <string.h>


// Macros.
/** Room for a path. */
#define WORLD_SPRITE_TEST_TEXT_CAPACITY 256


// Types.
/* A refused definition: its JSON, the error code and a fragment its message must hold. */
typedef struct RefusedDefinitionStruct
{
    const char* Json;
    ErrorCode Code;
    const char* Fragment;
} RefusedDefinition;


// Static functions: helpers.
/* Builds a world sprite definition from @p json without registering it and returns the constructor's result (the
 * definition, if one was built, is destroyed). */
static Error BuildDefinition(TestAssetFixture* fixture, const char* json)
{
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
    GenericBuffer_AppendString(&Bytes, (const unsigned char*)json);
    AssetDefinition* Definition = NULL;
    Error Result = AssetManager_BuildDefinition(fixture->Manager, fixture->Types.WorldSprite, &Bytes,
        (const unsigned char*)u8"test", &Definition);
    if (Definition != NULL)
    {
        Definition->VTable->Destroy(Definition);
    }
    Memory_Free(Bytes._data);
    return Result;
}

/* Loads the world sprite @p name under a new user; *outUser receives the user (retire it to release the hold). */
static WorldSprite* LoadWorldSprite(TestRun* run, TestAssetFixture* fixture, const char* name, AssetUserID* outUser)
{
    WorldSprite* Loaded = NULL;
    if (!TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(fixture->Manager, outUser))
        || !TEST_CHECK_SUCCESS(run, AssetManager_LoadWorldSprite(fixture->Manager, (const unsigned char*)name,
            *outUser, &Loaded)))
    {
        return NULL;
    }
    return Loaded;
}

/* Field by field (the struct has padding, which memcmp would compare too). */
static bool AreSettingsEqual(const WorldSpriteSettings* a, const WorldSpriteSettings* b)
{
    return (a->Orientation == b->Orientation) && (a->Pivot.x == b->Pivot.x) && (a->Pivot.y == b->Pivot.y)
        && (a->PixelsPerUnit == b->PixelsPerUnit) && (a->IsFlippedX == b->IsFlippedX)
        && (a->IsFlippedY == b->IsFlippedY) && (a->AlphaMode == b->AlphaMode) && (a->AlphaCutoff == b->AlphaCutoff)
        && (a->IsLit == b->IsLit) && (a->Emission == b->Emission) && (a->LightWrap == b->LightWrap)
        && (a->IsDecal == b->IsDecal) && (a->DecalOffset == b->DecalOffset)
        && (a->Render.HasOutline == b->Render.HasOutline) && (a->Render.OmitsPixelation == b->Render.OmitsPixelation)
        && (a->Render.CastsShadows == b->Render.CastsShadows) && (a->HasDrawDistance == b->HasDrawDistance)
        && (!a->HasDrawDistance || (a->DrawDistance == b->DrawDistance))
        && (a->IsDrawDistanceFixed == b->IsDrawDistanceFixed);
}

/* Creates an object with a sprite component that names no world sprite; returns the component, or NULL with a failed
 * check. */
static WorldSpriteComponent* AddPlacement(TestRun* run, World* world)
{
    WorldObject* Object = NULL;
    WorldComponent* Component = NULL;
    if (!TEST_CHECK_SUCCESS(run, World_CreateObject(world, NULL, &Object))
        || !TEST_CHECK_SUCCESS(run, World_AddComponent(world, WorldObject_GetId(Object), WorldComponentType_Sprite,
            &Component)))
    {
        return NULL;
    }
    return WorldSpriteComponent_FromComponent(Component);
}

/* A world sprite's settings unlike every default (and unlike the placement's values SetPlacementValues gives). */
static WorldSpriteSettings CreateAssetSettings(void)
{
    WorldSpriteSettings Settings = WorldSpriteSettings_CreateDefault();
    Settings.Orientation = WorldSpriteOrientation_AxisBillboard;
    Settings.Pivot = (Vector2){ 0.5f, 0.0f };
    Settings.PixelsPerUnit = 16.0f;
    Settings.IsFlippedX = false;
    Settings.IsFlippedY = false;
    Settings.AlphaMode = WorldSpriteAlphaMode_Blend;
    Settings.AlphaCutoff = 0.3f;
    Settings.IsLit = false;
    Settings.Emission = 2.0f;
    Settings.LightWrap = 0.25f;
    Settings.IsDecal = false;
    Settings.DecalOffset = 0.05f;
    Settings.Render = (WorldSpriteRenderSettings){ .HasOutline = false, .OmitsPixelation = false, .CastsShadows = false };
    Settings.HasDrawDistance = true;
    Settings.DrawDistance = 80.0f;
    Settings.IsDrawDistanceFixed = true;
    return Settings;
}

/* Gives a placement its own value of every setting, each unlike CreateAssetSettings' (the flags are left as they are). */
static void SetPlacementValues(TestRun* run, WorldSpriteComponent* placement)
{
    TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetOrientation(placement, WorldSpriteOrientation_Fixed));
    TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetPivot(placement, (Vector2){ 0.0f, 1.0f }));
    TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetPixelsPerUnit(placement, 64.0f));
    TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetFlippedX(placement, true));
    TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetFlippedY(placement, true));
    TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetAlphaMode(placement, WorldSpriteAlphaMode_Additive));
    TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetAlphaCutoff(placement, 0.9f));
    TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetLit(placement, true));
    TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetEmission(placement, 5.0f));
    TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetLightWrap(placement, 0.75f));
    TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetDecal(placement, true));
    TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetDecalOffset(placement, 0.2f));
    TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetCastsShadows(placement, true));
    TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetHasOutline(placement, true));
    TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetOmitsPixelation(placement, true));
    TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetDrawDistance(placement, 30.0f));
    TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetIsDrawDistanceFixed(placement, false));
}

/* Whether a resolved setting is the placement's own (true) or the asset's (false), for the values above. */
static bool IsSettingFromPlacement(const WorldSpriteSettings* resolved, WorldSpriteSetting setting)
{
    switch (setting)
    {
        case WorldSpriteSetting_Orientation:         return resolved->Orientation == WorldSpriteOrientation_Fixed;
        case WorldSpriteSetting_Pivot:               return resolved->Pivot.y == 1.0f;
        case WorldSpriteSetting_PixelsPerUnit:       return resolved->PixelsPerUnit == 64.0f;
        case WorldSpriteSetting_FlipX:               return resolved->IsFlippedX;
        case WorldSpriteSetting_FlipY:               return resolved->IsFlippedY;
        case WorldSpriteSetting_AlphaMode:           return resolved->AlphaMode == WorldSpriteAlphaMode_Additive;
        case WorldSpriteSetting_AlphaCutoff:         return resolved->AlphaCutoff == 0.9f;
        case WorldSpriteSetting_IsLit:               return resolved->IsLit;
        case WorldSpriteSetting_Emission:            return resolved->Emission == 5.0f;
        case WorldSpriteSetting_LightWrap:           return resolved->LightWrap == 0.75f;
        case WorldSpriteSetting_CastsShadows:        return resolved->Render.CastsShadows;
        case WorldSpriteSetting_IsDecal:             return resolved->IsDecal;
        case WorldSpriteSetting_DecalOffset:         return resolved->DecalOffset == 0.2f;
        case WorldSpriteSetting_HasOutline:          return resolved->Render.HasOutline;
        case WorldSpriteSetting_OmitPixelation:      return resolved->Render.OmitsPixelation;
        case WorldSpriteSetting_DrawDistance:        return resolved->DrawDistance == 30.0f;
        case WorldSpriteSetting_IsDrawDistanceFixed: return !resolved->IsDrawDistanceFixed;
        case WorldSpriteSetting_Count:               break;
    }
    return false;
}


// Tests: definitions.
/* A definition with just its name and its animation gets the defaults the sprite component had (T06), and no draw
 * distance of its own; it plays the animation it names. */
static void AMinimalDefinitionGetsTheDefaults(TestRun* run)
{
    TestAssetFixture Fixture;
    AssetUserID User = ASSET_USER_ID_INVALID;
    AssetUserID AnimationUser = ASSET_USER_ID_INVALID;
    SpriteAnimation* Animation = NULL;
    if (TestAssetFixture_Construct(run, &Fixture)
        && TestAssets_AddSpriteAnimation(run, &Fixture, "tree_animation", NULL)
        && TestAssets_AddWorldSpriteOf(run, &Fixture, "tree", "tree_animation")
        && TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(Fixture.Manager, &AnimationUser))
        && TEST_CHECK_SUCCESS(run, AssetManager_LoadSpriteAnimation(Fixture.Manager,
            (const unsigned char*)u8"tree_animation", AnimationUser, &Animation)))
    {
        WorldSprite* Loaded = LoadWorldSprite(run, &Fixture, "tree", &User);
        if (Loaded != NULL)
        {
            const WorldSpriteSettings* Settings = WorldSprite_GetSettings(Loaded);
            WorldSpriteSettings Defaults = WorldSpriteSettings_CreateDefault();
            TEST_CHECK(run, AreSettingsEqual(Settings, &Defaults));
            TEST_CHECK(run, (Settings->Orientation == WorldSpriteOrientation_Billboard) && (Settings->Pivot.x == 0.5f)
                && (Settings->Pivot.y == 0.5f) && (Settings->PixelsPerUnit == 32.0f) && !Settings->IsFlippedX
                && !Settings->IsFlippedY);
            TEST_CHECK(run, (Settings->AlphaMode == WorldSpriteAlphaMode_Cutout) && (Settings->AlphaCutoff == 0.5f)
                && Settings->IsLit && (Settings->Emission == 1.0f) && (Settings->LightWrap == 1.0f)
                && !Settings->IsDecal && (Settings->DecalOffset == 0.01f));
            TEST_CHECK(run, Settings->Render.HasOutline && !Settings->Render.OmitsPixelation
                && Settings->Render.CastsShadows);
            TEST_CHECK(run, !Settings->HasDrawDistance && !Settings->IsDrawDistanceFixed);
            TEST_CHECK(run, WorldSprite_GetAnimation(Loaded) == Animation); // the one shared animation asset
        }
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

/* The task's example, with a value unlike its default for every key. */
static void AFullDefinitionReadsEveryKey(TestRun* run)
{
    const char* Json =
        "{\n"
        "  \"name\": \"street_tree\",\n"
        "  \"sprite_animation\": \"street_tree\",\n"
        "  \"orientation\": \"axis_billboard\",\n"
        "  \"pivot\": [0.5, 0.0],\n"
        "  \"pixels_per_unit\": 24,\n"
        "  \"flip_x\": true,\n"
        "  \"flip_y\": true,\n"
        "  \"alpha_mode\": \"additive\",\n"
        "  \"alpha_cutoff\": 0.25,\n"
        "  \"is_lit\": false,\n"
        "  \"emission\": 1.5,\n"
        "  \"light_wrap\": 0.75,\n"
        "  \"is_decal\": true,\n"
        "  \"decal_offset\": 0.02,\n"
        "  \"render\": { \"has_outline\": false, \"omit_pixelation\": true, \"casts_shadows\": false },\n"
        "  \"draw_distance\": 90,\n"
        "  \"is_draw_distance_fixed\": true\n"
        "}\n";
    TestAssetFixture Fixture;
    AssetUserID User = ASSET_USER_ID_INVALID;
    WorldSprite* Loaded = NULL;
    if (TestAssetFixture_Construct(run, &Fixture) && TestAssets_AddSpriteAnimation(run, &Fixture, "street_tree", NULL)
        && TestAssets_AddWorldSprite(run, &Fixture, Json))
    {
        Loaded = LoadWorldSprite(run, &Fixture, "street_tree", &User);
    }
    if (Loaded != NULL)
    {
        const WorldSpriteSettings* Settings = WorldSprite_GetSettings(Loaded);
        TEST_CHECK(run, (Settings->Orientation == WorldSpriteOrientation_AxisBillboard) && (Settings->Pivot.x == 0.5f)
            && (Settings->Pivot.y == 0.0f) && (Settings->PixelsPerUnit == 24.0f) && Settings->IsFlippedX
            && Settings->IsFlippedY);
        TEST_CHECK(run, (Settings->AlphaMode == WorldSpriteAlphaMode_Additive) && (Settings->AlphaCutoff == 0.25f)
            && !Settings->IsLit && (Settings->Emission == 1.5f) && (Settings->LightWrap == 0.75f) && Settings->IsDecal
            && (Settings->DecalOffset == 0.02f));
        TEST_CHECK(run, !Settings->Render.HasOutline && Settings->Render.OmitsPixelation
            && !Settings->Render.CastsShadows);
        TEST_CHECK(run, Settings->HasDrawDistance && (Settings->DrawDistance == 90.0f) && Settings->IsDrawDistanceFixed);
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

/* Every rule refuses what it should, with a message that says where and why: the definition's own checks, unknown keys
 * in the root and in "render" (the keys allowed there listed), and the settings' rules, which are the sprite
 * component's. */
static void EachRuleIsRefusedWithAClearMessage(TestRun* run)
{
    static const RefusedDefinition Cases[] =
    {
        { "{\"name\": \"s\"}", ErrorCode_InvalidAssetDefinition,
            "World sprite \"s\": the definition needs a \"sprite_animation\"" },
        { "{\"name\": \"s\", \"sprite_animation\": 7}", ErrorCode_InvalidAssetDefinition,
            "sprite_animation must be a string" },
        { "{\"name\": \"s\", \"sprite_animation\": \"Tree\"}", ErrorCode_InvalidAssetDefinition,
            "sprite_animation \"Tree\" is not a valid asset name" },
        { "{\"name\": \"s\", \"sprite_animation\": \"t\", \"pixels_per_metre\": 32}",
            ErrorCode_InvalidAssetDefinition, "Unknown key \"pixels_per_metre\" in world sprite \"s\"; the keys allowed "
                "there are: name, sprite_animation, render, orientation, pivot, pixels_per_unit" },
        { "{\"name\": \"s\", \"sprite_animation\": \"t\", \"has_outline\": false}",
            ErrorCode_InvalidAssetDefinition, "Unknown key \"has_outline\" in world sprite \"s\";" },
        { "{\"name\": \"s\", \"sprite_animation\": \"t\", \"\\u001b[31m\": 1}", ErrorCode_InvalidAssetDefinition,
            "Unknown key \"?[31m\"" },
        { "{\"name\": \"s\", \"sprite_animation\": \"t\", \"render\": 4}", ErrorCode_InvalidAssetDefinition,
            "World sprite \"s\": render must be a JSON object." },
        { "{\"name\": \"s\", \"sprite_animation\": \"t\", \"render\": {\"outline\": true}}",
            ErrorCode_InvalidAssetDefinition, "Unknown key \"outline\" in world sprite \"s\", render; the keys allowed "
                "there are: has_outline, omit_pixelation, casts_shadows." },
        { "{\"name\": \"s\", \"sprite_animation\": \"t\", \"render\": {\"pivot\": [0, 0]}}",
            ErrorCode_InvalidAssetDefinition, "Unknown key \"pivot\" in world sprite \"s\", render;" },
        { "{\"name\": \"s\", \"sprite_animation\": \"t\", \"render\": {\"has_outline\": \"yes\"}}",
            ErrorCode_InvalidJSON, "World sprite \"s\": world_sprite_render.has_outline: " },
        { "{\"name\": \"s\", \"sprite_animation\": \"t\", \"pixels_per_unit\": 0}", ErrorCode_InvalidJSON,
            "World sprite \"s\": world_sprite.pixels_per_unit (PixelsPerUnit) must be finite and > 0." },
        { "{\"name\": \"s\", \"sprite_animation\": \"t\", \"pixels_per_unit\": -2}", ErrorCode_InvalidJSON,
            "world_sprite.pixels_per_unit (PixelsPerUnit) must be finite and > 0." },
        { "{\"name\": \"s\", \"sprite_animation\": \"t\", \"alpha_cutoff\": 1.5}", ErrorCode_InvalidJSON,
            "world_sprite.alpha_cutoff (AlphaCutoff) must be finite and in [0, 1]." },
        { "{\"name\": \"s\", \"sprite_animation\": \"t\", \"emission\": -1}", ErrorCode_InvalidJSON,
            "world_sprite.emission (Emission) must be finite and >= 0." },
        { "{\"name\": \"s\", \"sprite_animation\": \"t\", \"light_wrap\": 2}", ErrorCode_InvalidJSON,
            "world_sprite.light_wrap (LightWrap) must be finite and in [0, 1]." },
        { "{\"name\": \"s\", \"sprite_animation\": \"t\", \"decal_offset\": -0.5}", ErrorCode_InvalidJSON,
            "world_sprite.decal_offset (DecalOffset) must be finite and >= 0." },
        { "{\"name\": \"s\", \"sprite_animation\": \"t\", \"draw_distance\": 0}", ErrorCode_InvalidJSON,
            "world_sprite.draw_distance (DrawDistance) must be finite and > 0." },
        { "{\"name\": \"s\", \"sprite_animation\": \"t\", \"draw_distance\": \"far\"}", ErrorCode_InvalidJSON,
            "World sprite \"s\": world_sprite.draw_distance: " },
        { "{\"name\": \"s\", \"sprite_animation\": \"t\", \"draw_distance\": 1e40}", ErrorCode_InvalidJSON,
            "world_sprite.draw_distance: The number does not fit a float." },
        { "{\"name\": \"s\", \"sprite_animation\": \"t\", \"orientation\": \"upright\"}", ErrorCode_InvalidJSON,
            "world_sprite.orientation: Expected the sprite_orientation name, one of" },
        { "{\"name\": \"s\", \"sprite_animation\": \"t\", \"alpha_mode\": \"glow\"}", ErrorCode_InvalidJSON,
            "world_sprite.alpha_mode: Expected the sprite_alpha_mode name, one of" },
        { "{\"name\": \"s\", \"sprite_animation\": \"t\", \"pivot\": [0.5]}", ErrorCode_InvalidJSON,
            "world_sprite.pivot: Vector array requires exactly two elements." },
        { "{\"name\": \"s\", \"sprite_animation\": \"t\", \"flip_x\": 3}", ErrorCode_InvalidJSON,
            "world_sprite.flip_x: " },
        { "{\"name\": \"s\", \"sprite_animation\": \"t\", \"has_draw_distance\": 3}", ErrorCode_InvalidJSON,
            "world_sprite.has_draw_distance: " },
        { "{\"name\": \"s\", \"sprite_animation\": \"t\", \"is_draw_distance_fixed\": \"maybe\"}",
            ErrorCode_InvalidJSON, "world_sprite.is_draw_distance_fixed: " },
    };
    TestAssetFixture Fixture;
    if (TestAssetFixture_Construct(run, &Fixture))
    {
        for (size_t Index = 0U; Index < (sizeof(Cases) / sizeof(Cases[0])); Index++)
        {
            const char* const Fragments[] = { Cases[Index].Fragment, NULL };
            if (!TestText_CheckErrorMessage(run, BuildDefinition(&Fixture, Cases[Index].Json), Cases[Index].Code,
                Fragments))
            {
                printf("      (case %u: %s)\n", (unsigned int)Index, Cases[Index].Json);
            }
        }
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

/* The world sprite's tables are built from the sprite component's rows: every key the definition reads is a component
 * row with the same type, default, rules, names and texts, so a placement's own value follows the same rules as the
 * asset's. The one asset-only row, has_draw_distance, is a Bool off by default right before draw_distance, where the
 * component has its overrides_draw_distance. */
static void TheDefinitionSharesTheComponentsRows(TestRun* run)
{
    const PropertyTable* Component = WorldSpriteComponent_GetPropertyTable();
    const PropertyTable* const Tables[] = { WorldSprite_GetSettingsPropertyTable(), WorldSprite_GetRenderPropertyTable() };
    size_t RowCount = 0U;
    for (size_t Table = 0U; Table < (sizeof(Tables) / sizeof(Tables[0])); Table++)
    {
        TEST_CHECK_SUCCESS(run, PropertyTable_Check(Tables[Table]));
        for (size_t Index = 0U; Index < Tables[Table]->DescriptorCount; Index++)
        {
            const PropertyDescriptor* Asset = &Tables[Table]->Descriptors[Index];
            if (strcmp((const char*)Asset->Key, "has_draw_distance") == 0)
            {
                const PropertyDescriptor* Value = Asset + 1;
                TEST_CHECK(run, (Asset->Type == PropertyType_Bool) && !Asset->Default.BoolValue
                    && ((Index + 1U) < Tables[Table]->DescriptorCount)
                    && (strcmp((const char*)Value->Key, "draw_distance") == 0)
                    && (PropertyTable_FindByKey(Component, (const unsigned char*)u8"overrides_draw_distance")->WireId
                        == Asset->WireId));
                continue;
            }
            const PropertyDescriptor* Placement = PropertyTable_FindByKey(Component, Asset->Key);
            if (!TEST_CHECK(run, Placement != NULL))
            {
                continue;
            }
            TEST_CHECK(run, (Placement->Type == Asset->Type) && (Placement->WireId == Asset->WireId)
                && (Placement->Flags == Asset->Flags) && (Placement->EnumTable == Asset->EnumTable)
                && (memcmp(&Placement->Rules, &Asset->Rules, sizeof(PropertyRules)) == 0)
                && (memcmp(&Placement->Default, &Asset->Default, sizeof(PropertyData)) == 0));
            // The same texts (two copies of one literal in two files need not share an address).
            TEST_CHECK(run, (strcmp((const char*)Placement->DisplayName, (const char*)Asset->DisplayName) == 0)
                && (strcmp((const char*)Placement->Description, (const char*)Asset->Description) == 0)
                && (strcmp((const char*)Placement->Category, (const char*)Asset->Category) == 0));
            RowCount++;
        }
    }
    // Every setting a placement can override is one of them.
    TEST_CHECK(run, RowCount == (size_t)WorldSpriteSetting_Count);
}


/* Writes @p settings through the two tables (every key when @p areDefaultsWritten) into a definition named @p name
 * playing "leaves", registers it and loads it; *outLoaded receives what it reads back as. */
static bool RoundTripThroughTheTables(TestRun* run, TestAssetFixture* fixture, const WorldSpriteSettings* settings,
    const char* name, bool areDefaultsWritten, WorldSpriteSettings* outLoaded)
{
    JSONObjectPool* Pool = NULL;
    JSONCompound* Root = NULL;
    JSONCompound* Render = NULL;
    GenericBuffer RootText;
    GenericBuffer RenderText;
    GenericBuffer Json;
    GenericBuffer_AllocateVariable(&RootText, WORLD_SPRITE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    GenericBuffer_AllocateVariable(&RenderText, WORLD_SPRITE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    GenericBuffer_AllocateVariable(&Json, WORLD_SPRITE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    PropertyWriteOptions Options = PropertyWriteOptions_CreateDefault();
    Options.AreDefaultsWritten = areDefaultsWritten;
    bool IsRead = false;
    if (TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool))
        && TEST_CHECK_SUCCESS(run, JSONObjectPool_BorrowCompound(Pool, &Root))
        && TEST_CHECK_SUCCESS(run, JSONObjectPool_BorrowCompound(Pool, &Render))
        && TEST_CHECK_SUCCESS(run, Properties_WriteJSON(WorldSprite_GetSettingsPropertyTable(), settings, Pool, Root,
            &Options))
        && TEST_CHECK_SUCCESS(run, Properties_WriteJSON(WorldSprite_GetRenderPropertyTable(), &settings->Render, Pool,
            Render, &Options)))
    {
        JSONObjectValue RootValue = JSONObjectValue_CreateCompound(Root);
        JSONObjectValue RenderValue = JSONObjectValue_CreateCompound(Render);
        if (TEST_CHECK_SUCCESS(run, JSON_Serialize(&RootValue, &RootText, JSONSerializeFlags_NullTerminated))
            && TEST_CHECK_SUCCESS(run, JSON_Serialize(&RenderValue, &RenderText, JSONSerializeFlags_NullTerminated)))
        {
            // The root's keys after the definition's own: drop the root text's braces.
            const char* Keys = (const char*)RootText._data + 1;
            int KeyLength = (int)strlen(Keys) - 1;
            Error Result = StringUTF8_Format(
                u8"{\"name\": \"%s\", \"sprite_animation\": \"leaves\", \"render\": %s%s%.*s}", &Json, name,
                (const char*)RenderText._data, (KeyLength > 0) ? ", " : "", KeyLength, Keys);
            AssetUserID User = ASSET_USER_ID_INVALID;
            WorldSprite* Loaded = NULL;
            if (TEST_CHECK_SUCCESS(run, Result) && TestAssets_AddWorldSprite(run, fixture, (const char*)Json._data)
                && ((Loaded = LoadWorldSprite(run, fixture, name, &User)) != NULL))
            {
                *outLoaded = *WorldSprite_GetSettings(Loaded);
                IsRead = true;
            }
        }
    }
    if (Root != NULL)
    {
        TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnCompound(Pool, Root, true));
    }
    if (Render != NULL)
    {
        TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnCompound(Pool, Render, true));
    }
    if (Pool != NULL)
    {
        TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
    }
    Memory_Free(Json._data);
    Memory_Free(RenderText._data);
    Memory_Free(RootText._data);
    return IsRead;
}

/* Whether a world sprite gives its own draw distance is the explicit row has_draw_distance. A hand-written
 * draw_distance without it is on; an explicit flag wins (off: the kind's distance, the value kept). Settings written
 * through the two tables read back the same: every key written, or the defaults left out while nothing is kept unused
 * (a decal's own 150 m, the row's default value, stays its own). */
static void TheDrawDistanceFlagIsExplicitAndRoundTrips(TestRun* run)
{
    static const struct
    {
        const char* Json;
        bool HasDrawDistance;
        float DrawDistance;
    }
    Cases[] =
    {
        { "{\"name\": \"plain\", \"sprite_animation\": \"leaves\"}", false, 150.0f },
        { "{\"name\": \"given\", \"sprite_animation\": \"leaves\", \"draw_distance\": 90}", true, 90.0f },
        { "{\"name\": \"kept\", \"sprite_animation\": \"leaves\", \"has_draw_distance\": false, \"draw_distance\": 90}",
            false, 90.0f },
        { "{\"name\": \"flagged\", \"sprite_animation\": \"leaves\", \"has_draw_distance\": true}", true, 150.0f },
    };
    TestAssetFixture Fixture;
    if (!TestAssetFixture_Construct(run, &Fixture) || !TestAssets_AddSpriteAnimation(run, &Fixture, "leaves", NULL))
    {
        TestAssetFixture_Deconstruct(run, &Fixture);
        return;
    }
    for (size_t Index = 0U; Index < (sizeof(Cases) / sizeof(Cases[0])); Index++)
    {
        char Name[16];
        AssetUserID User = ASSET_USER_ID_INVALID;
        WorldSprite* Loaded = NULL;
        sscanf(Cases[Index].Json, "{\"name\": \"%15[a-z]\"", Name);
        if (TestAssets_AddWorldSprite(run, &Fixture, Cases[Index].Json)
            && ((Loaded = LoadWorldSprite(run, &Fixture, Name, &User)) != NULL))
        {
            const WorldSpriteSettings* Settings = WorldSprite_GetSettings(Loaded);
            TEST_CHECK(run, (Settings->HasDrawDistance == Cases[Index].HasDrawDistance)
                && (Settings->DrawDistance == Cases[Index].DrawDistance));
        }
    }

    // The table's own flag: set by key like any setting.
    WorldSpriteSettings Decal = WorldSpriteSettings_CreateDefault();
    PropertyValue On = PropertyValue_FromBool(true);
    TEST_CHECK_SUCCESS(run, Properties_Set(WorldSprite_GetSettingsPropertyTable(), &Decal,
        (const unsigned char*)u8"has_draw_distance", &On));
    TEST_CHECK(run, Decal.HasDrawDistance && (Decal.DrawDistance == WORLD_SPRITE_DEFAULT_DRAW_DISTANCE));
    Decal.Orientation = WorldSpriteOrientation_Fixed;
    Decal.IsDecal = true;

    // A decal's own 150 m (the default value) stays its own, with or without the defaults written; a value kept while
    // the flag is off stays kept when every key is written.
    WorldSpriteSettings Kept = WorldSpriteSettings_CreateDefault();
    Kept.DrawDistance = 90.0f;
    Kept.Render.CastsShadows = false;
    WorldSpriteSettings Read;
    if (RoundTripThroughTheTables(run, &Fixture, &Decal, "decal_short", false, &Read))
    {
        TEST_CHECK(run, AreSettingsEqual(&Read, &Decal));
    }
    if (RoundTripThroughTheTables(run, &Fixture, &Decal, "decal_full", true, &Read))
    {
        TEST_CHECK(run, AreSettingsEqual(&Read, &Decal));
    }
    if (RoundTripThroughTheTables(run, &Fixture, &Kept, "kept_full", true, &Read))
    {
        TEST_CHECK(run, AreSettingsEqual(&Read, &Kept) && (Read.DrawDistance == 90.0f) && !Read.HasDrawDistance);
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}


// Tests: loading.
/* Holding a world sprite holds its animation (a dependency): two world sprites over one animation load it once, and it
 * unloads when the last of them does. */
static void LoadingHoldsTheAnimationUntilTheLastHolderGoes(TestRun* run)
{
    TestAssetFixture Fixture;
    TestAssetCounters Counters = { 0 };
    AssetUserID First = ASSET_USER_ID_INVALID;
    AssetUserID Second = ASSET_USER_ID_INVALID;
    bool IsBuilt = TestAssetFixture_Construct(run, &Fixture)
        && TestAssets_AddSpriteAnimation(run, &Fixture, "leaves", &Counters)
        && TestAssets_AddWorldSpriteOf(run, &Fixture, "tree", "leaves")
        && TestAssets_AddWorldSprite(run, &Fixture, "{\"name\": \"tree_mirrored\", \"sprite_animation\": \"leaves\", "
            "\"flip_x\": true}");
    WorldSprite* Tree = IsBuilt ? LoadWorldSprite(run, &Fixture, "tree", &First) : NULL;
    WorldSprite* Mirrored = (Tree != NULL) ? LoadWorldSprite(run, &Fixture, "tree_mirrored", &Second) : NULL;
    if (Mirrored != NULL)
    {
        TEST_CHECK(run, (Counters.LoadCount == 1U)
            && (WorldSprite_GetAnimation(Tree) == WorldSprite_GetAnimation(Mirrored)));
        TEST_CHECK(run, !WorldSprite_GetSettings(Tree)->IsFlippedX && WorldSprite_GetSettings(Mirrored)->IsFlippedX);

        TEST_CHECK_SUCCESS(run, AssetManager_RetireUser(Fixture.Manager, First));
        TEST_CHECK(run, Counters.UnloadCount == 0U);
        TEST_CHECK_SUCCESS(run, AssetManager_RetireUser(Fixture.Manager, Second));
        TEST_CHECK(run, Counters.UnloadCount == 1U);
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

/* An animation that cannot load fails the world sprite, naming it. */
static void AMissingAnimationFailsTheLoad(TestRun* run)
{
    TestAssetFixture Fixture;
    AssetUserID User = ASSET_USER_ID_INVALID;
    if (TestAssetFixture_Construct(run, &Fixture) && TestAssets_AddWorldSpriteOf(run, &Fixture, "broken", "nowhere")
        && TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(Fixture.Manager, &User)))
    {
        WorldSprite* Loaded = NULL;
        const char* const Fragments[] =
        {
            "World sprite \"broken\": sprite animation \"nowhere\": No definition named \"nowhere\"", NULL
        };
        TestText_CheckErrorMessage(run, AssetManager_LoadWorldSprite(Fixture.Manager, (const unsigned char*)u8"broken",
            User, &Loaded), ErrorCode_InvalidOperation, Fragments);
        TEST_CHECK(run, Loaded == NULL);
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}


// Tests: the sprite component's overrides.
/* Every setting a placement can override is a flag row "overrides_<key>" (a Bool, off by default) right before its
 * value row, and the typed accessors reach the same fields as the table. */
static void EveryOverrideIsAFlagNextToItsValue(TestRun* run)
{
    static const char* const ValueKeys[WorldSpriteSetting_Count] =
    {
        [WorldSpriteSetting_Orientation] = "orientation",
        [WorldSpriteSetting_Pivot] = "pivot",
        [WorldSpriteSetting_PixelsPerUnit] = "pixels_per_unit",
        [WorldSpriteSetting_FlipX] = "flip_x",
        [WorldSpriteSetting_FlipY] = "flip_y",
        [WorldSpriteSetting_AlphaMode] = "alpha_mode",
        [WorldSpriteSetting_AlphaCutoff] = "alpha_cutoff",
        [WorldSpriteSetting_IsLit] = "is_lit",
        [WorldSpriteSetting_Emission] = "emission",
        [WorldSpriteSetting_LightWrap] = "light_wrap",
        [WorldSpriteSetting_CastsShadows] = "casts_shadows",
        [WorldSpriteSetting_IsDecal] = "is_decal",
        [WorldSpriteSetting_DecalOffset] = "decal_offset",
        [WorldSpriteSetting_HasOutline] = "has_outline",
        [WorldSpriteSetting_OmitPixelation] = "omit_pixelation",
        [WorldSpriteSetting_DrawDistance] = "draw_distance",
        [WorldSpriteSetting_IsDrawDistanceFixed] = "is_draw_distance_fixed",
    };
    const PropertyTable* Table = WorldSpriteComponent_GetPropertyTable();
    TEST_CHECK_SUCCESS(run, PropertyTable_Check(Table));
    World TestWorld;
    KeyRegistry Keys;
    if (!TestWorld_Construct(run, &TestWorld, &Keys, NULL, NULL))
    {
        return;
    }
    WorldSpriteComponent* Placement = AddPlacement(run, &TestWorld);
    if (Placement != NULL)
    {
        WorldComponent* Component = WorldSpriteComponent_AsComponent(Placement);
        for (int Setting = 0; Setting < (int)WorldSpriteSetting_Count; Setting++)
        {
            char FlagKey[64];
            snprintf(FlagKey, sizeof(FlagKey), "overrides_%s", ValueKeys[Setting]);
            const PropertyDescriptor* Flag = PropertyTable_FindByKey(Table, (const unsigned char*)FlagKey);
            const PropertyDescriptor* Value = PropertyTable_FindByKey(Table, (const unsigned char*)ValueKeys[Setting]);
            if (!TEST_CHECK(run, (Flag != NULL) && (Value != NULL)))
            {
                continue;
            }
            TEST_CHECK(run, (Flag->Type == PropertyType_Bool) && !Flag->Default.BoolValue && ((Flag + 1) == Value));

            // The typed flag accessors and the table reach the same field, and both advance the revision.
            TEST_CHECK(run, !WorldSpriteComponent_IsOverridden(Placement, (WorldSpriteSetting)Setting));
            uint32_t Revision = WorldComponent_GetRevision(Component);
            TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetOverridden(Placement, (WorldSpriteSetting)Setting, true));
            PropertyValue Read = { 0 };
            TEST_CHECK(run, TEST_CHECK_SUCCESS(run, Properties_Get(Table, Placement, Flag->Key, &Read))
                && Read.Data.BoolValue && (WorldComponent_GetRevision(Component) == (Revision + 1U)));
            PropertyValue Off = PropertyValue_FromBool(false);
            TEST_CHECK_SUCCESS(run, Properties_Set(Table, Placement, Flag->Key, &Off));
            TEST_CHECK(run, !WorldSpriteComponent_IsOverridden(Placement, (WorldSpriteSetting)Setting)
                && (WorldComponent_GetRevision(Component) == (Revision + 2U)));
        }
        TEST_CHECK_ERROR(run, WorldSpriteComponent_SetOverridden(Placement, WorldSpriteSetting_Count, true),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, !WorldSpriteComponent_IsOverridden(Placement, (WorldSpriteSetting)42));
    }
    TestWorld_Deconstruct(run, &TestWorld, &Keys);
}

/* The settings in effect: the world sprite's while every flag is off (whatever the placement's own values), the
 * placement's own value for each flag that is on (each alone), and the values kept while their flags are off. */
static void TheFlagsChooseBetweenTheWorldSpriteAndThePlacement(TestRun* run)
{
    World TestWorld;
    KeyRegistry Keys;
    if (!TestWorld_Construct(run, &TestWorld, &Keys, NULL, NULL))
    {
        return;
    }
    WorldSpriteComponent* Placement = AddPlacement(run, &TestWorld);
    if (Placement != NULL)
    {
        WorldSpriteSettings Asset = CreateAssetSettings();
        WorldSpriteSettings Resolved = WorldSpriteComponent_ResolveSettings(Placement, &Asset);
        TEST_CHECK(run, AreSettingsEqual(&Resolved, &Asset));

        // The placement's own values, all unlike the asset's; with their flags off nothing changes.
        SetPlacementValues(run, Placement);
        Resolved = WorldSpriteComponent_ResolveSettings(Placement, &Asset);
        TEST_CHECK(run, AreSettingsEqual(&Resolved, &Asset));

        // Each flag alone takes exactly its own setting from the placement.
        for (int Setting = 0; Setting < (int)WorldSpriteSetting_Count; Setting++)
        {
            TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetOverridden(Placement, (WorldSpriteSetting)Setting, true));
            Resolved = WorldSpriteComponent_ResolveSettings(Placement, &Asset);
            for (int Other = 0; Other < (int)WorldSpriteSetting_Count; Other++)
            {
                if (!TEST_CHECK(run, IsSettingFromPlacement(&Resolved, (WorldSpriteSetting)Other) == (Other == Setting)))
                {
                    printf("      (flag %d, setting %d)\n", Setting, Other);
                }
            }
            TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetOverridden(Placement, (WorldSpriteSetting)Setting, false));
        }

        // Every flag on: the placement's settings throughout.
        for (int Setting = 0; Setting < (int)WorldSpriteSetting_Count; Setting++)
        {
            TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetOverridden(Placement, (WorldSpriteSetting)Setting, true));
        }
        Resolved = WorldSpriteComponent_ResolveSettings(Placement, &Asset);
        for (int Setting = 0; Setting < (int)WorldSpriteSetting_Count; Setting++)
        {
            TEST_CHECK(run, IsSettingFromPlacement(&Resolved, (WorldSpriteSetting)Setting));
        }
        TEST_CHECK(run, Resolved.HasDrawDistance && WorldSpriteSettings_IsDrawnAsDecal(&Resolved));

        // Off and on again: the placement's value was kept.
        TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetOverridden(Placement, WorldSpriteSetting_Pivot, false));
        TEST_CHECK(run, WorldSpriteComponent_ResolveSettings(Placement, &Asset).Pivot.y == 0.0f);
        TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetOverridden(Placement, WorldSpriteSetting_Pivot, true));
        TEST_CHECK(run, WorldSpriteComponent_ResolveSettings(Placement, &Asset).Pivot.y == 1.0f);
    }
    TestWorld_Deconstruct(run, &TestWorld, &Keys);
}

/* The draw distance in effect: the placement's own while it overrides it, else the world sprite's, else the kind's
 * default (sprites 150 m, decals 60 m) for the settings in effect, so a placement that makes a decal gets the decals';
 * times the multiplier in every case. Fixed as the world sprite says, or as the placement says while it overrides it. */
static void TheDrawDistanceComesFromThePlacementTheWorldSpriteOrTheKind(TestRun* run)
{
    World TestWorld;
    KeyRegistry Keys;
    if (!TestWorld_Construct(run, &TestWorld, &Keys, NULL, NULL))
    {
        return;
    }
    WorldSpriteComponent* Placement = AddPlacement(run, &TestWorld);
    if (Placement != NULL)
    {
        WorldSpriteSettings Plain = WorldSpriteSettings_CreateDefault();
        WorldSpriteSettings Decal = WorldSpriteSettings_CreateDefault();
        Decal.Orientation = WorldSpriteOrientation_Fixed;
        Decal.IsDecal = true;
        WorldSpriteSettings MarkedBillboard = WorldSpriteSettings_CreateDefault();
        MarkedBillboard.IsDecal = true; // a "decal" billboard is drawn as a sprite
        WorldSpriteSettings Given = WorldSpriteSettings_CreateDefault();
        Given.HasDrawDistance = true;
        Given.DrawDistance = 40.0f;

        // No distance anywhere: the kind's, decided on the settings in effect.
        WorldSpriteSettings Resolved = WorldSpriteComponent_ResolveSettings(Placement, &Plain);
        TEST_CHECK(run, Resolved.HasDrawDistance && (Resolved.DrawDistance == WORLD_SPRITE_DEFAULT_DRAW_DISTANCE));
        TEST_CHECK(run, WorldSpriteComponent_ResolveSettings(Placement, &Decal).DrawDistance
            == WORLD_SPRITE_DEFAULT_DECAL_DRAW_DISTANCE);
        TEST_CHECK(run, WorldSpriteComponent_ResolveSettings(Placement, &MarkedBillboard).DrawDistance
            == WORLD_SPRITE_DEFAULT_DRAW_DISTANCE);
        TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetOverridden(Placement, WorldSpriteSetting_Orientation, true));
        TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetOrientation(Placement, WorldSpriteOrientation_Fixed));
        TEST_CHECK(run, WorldSpriteComponent_ResolveSettings(Placement, &MarkedBillboard).DrawDistance
            == WORLD_SPRITE_DEFAULT_DECAL_DRAW_DISTANCE); // this placement makes it a decal
        TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetOrientation(Placement, WorldSpriteOrientation_Billboard));
        TEST_CHECK(run, WorldSpriteComponent_ResolveSettings(Placement, &Decal).DrawDistance
            == WORLD_SPRITE_DEFAULT_DRAW_DISTANCE); // and this one a sprite again
        TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetOverridden(Placement, WorldSpriteSetting_Orientation, false));

        // The world sprite's; the multiplier on whichever is in effect; the placement's own while it overrides it.
        TEST_CHECK(run, WorldSpriteComponent_ResolveSettings(Placement, &Given).DrawDistance == 40.0f);
        TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetDrawDistanceMultiplier(Placement, 2.0f));
        TEST_CHECK(run, WorldSpriteComponent_ResolveSettings(Placement, &Given).DrawDistance == 80.0f);
        TEST_CHECK(run, WorldSpriteComponent_ResolveSettings(Placement, &Decal).DrawDistance == 120.0f);
        TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetDrawDistance(Placement, 10.0f));
        TEST_CHECK(run, WorldSpriteComponent_ResolveSettings(Placement, &Given).DrawDistance == 80.0f); // flag off
        TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetOverridden(Placement, WorldSpriteSetting_DrawDistance, true));
        TEST_CHECK(run, (WorldSpriteComponent_ResolveSettings(Placement, &Given).DrawDistance == 20.0f)
            && (WorldSpriteComponent_ResolveSettings(Placement, &Plain).DrawDistance == 20.0f));

        // Fixed: the world sprite's, or the placement's own either way while it overrides it.
        Given.IsDrawDistanceFixed = true;
        TEST_CHECK(run, WorldSpriteComponent_ResolveSettings(Placement, &Given).IsDrawDistanceFixed
            && !WorldSpriteComponent_ResolveSettings(Placement, &Plain).IsDrawDistanceFixed);
        TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetOverridden(Placement, WorldSpriteSetting_IsDrawDistanceFixed,
            true));
        TEST_CHECK(run, !WorldSpriteComponent_ResolveSettings(Placement, &Given).IsDrawDistanceFixed);
        TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetIsDrawDistanceFixed(Placement, true));
        TEST_CHECK(run, WorldSpriteComponent_ResolveSettings(Placement, &Plain).IsDrawDistanceFixed);

        // A product past the float range is +infinity: seen from everywhere, never a broken number.
        TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetDrawDistance(Placement, 3e38f));
        TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetDrawDistanceMultiplier(Placement, 1e10f));
        TEST_CHECK(run, isinf(WorldSpriteComponent_ResolveSettings(Placement, &Given).DrawDistance));
    }
    TestWorld_Deconstruct(run, &TestWorld, &Keys);
}

/* Changing the world sprite's name keeps the object and its overrides, advances the revision and binds the new world
 * sprite; the settings in effect follow it. */
static void SwappingTheWorldSpriteKeepsTheObjectAndItsOverrides(TestRun* run)
{
    TestAssetFixture Fixture;
    World TestWorld;
    KeyRegistry Keys;
    if (!TestAssetFixture_Construct(run, &Fixture) || !TestAssets_AddSpriteAnimation(run, &Fixture, "leaves", NULL)
        || !TestAssets_AddSpriteAnimation(run, &Fixture, "twigs", NULL)
        || !TestAssets_AddWorldSprite(run, &Fixture, "{\"name\": \"tree\", \"sprite_animation\": \"leaves\", "
            "\"orientation\": \"axis_billboard\", \"draw_distance\": 90}")
        || !TestAssets_AddWorldSprite(run, &Fixture, "{\"name\": \"bush\", \"sprite_animation\": \"twigs\", "
            "\"pixels_per_unit\": 64}")
        || !TestWorld_Construct(run, &TestWorld, &Keys, Fixture.Manager, NULL))
    {
        TestAssetFixture_Deconstruct(run, &Fixture);
        return;
    }

    WorldSpriteComponent* Placement = AddPlacement(run, &TestWorld);
    if ((Placement != NULL)
        && TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetWorldSprite(Placement, (const unsigned char*)u8"tree"))
        && TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetOverridden(Placement, WorldSpriteSetting_FlipX, true))
        && TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetFlippedX(Placement, true)))
    {
        const WorldObject* Owner = WorldComponent_GetOwner(WorldSpriteComponent_AsComponent(Placement));
        uint64_t Id = WorldObject_GetId(Owner);
        const WorldSprite* Tree = WorldSpriteComponent_GetBoundWorldSprite(Placement);
        TEST_CHECK(run, Tree != NULL);
        uint32_t Revision = WorldComponent_GetRevision(WorldSpriteComponent_AsComponent(Placement));

        TEST_CHECK_SUCCESS(run, WorldSpriteComponent_SetWorldSprite(Placement, (const unsigned char*)u8"bush"));
        const WorldSprite* Bush = WorldSpriteComponent_GetBoundWorldSprite(Placement);
        TEST_CHECK(run, (WorldComponent_GetRevision(WorldSpriteComponent_AsComponent(Placement)) == (Revision + 1U))
            && (Bush != NULL) && (Bush != Tree));
        TEST_CHECK(run, (WorldComponent_GetOwner(WorldSpriteComponent_AsComponent(Placement)) == Owner)
            && (WorldObject_GetId(Owner) == Id)
            && WorldSpriteComponent_IsOverridden(Placement, WorldSpriteSetting_FlipX));
        if (Bush != NULL)
        {
            TEST_CHECK(run, SpriteAnimationInstance_GetSource(WorldSpriteComponent_GetAnimationInstance(Placement))
                == WorldSprite_GetAnimation(Bush));
            WorldSpriteSettings Resolved = WorldSpriteComponent_ResolveSettings(Placement,
                WorldSprite_GetSettings(Bush));
            TEST_CHECK(run, (Resolved.Orientation == WorldSpriteOrientation_Billboard)
                && (Resolved.PixelsPerUnit == 64.0f) && Resolved.IsFlippedX
                && (Resolved.DrawDistance == WORLD_SPRITE_DEFAULT_DRAW_DISTANCE));
        }
    }
    TestWorld_Deconstruct(run, &TestWorld, &Keys);
    TestAssetFixture_Deconstruct(run, &Fixture);
}


// Tests: the shipped content.
/* The game's world sprites (compile/out/asset/world_sprites) are all read without a skipped file. */
static void TheShippedWorldSpritesAreRead(TestRun* run)
{
    static const char* const Names[] =
    {
        "test_sprite", "test_tire_mark", "test_tree", "test_sign", "test_smoke", "test_glow", "test_neon"
    };
    TestAssetFixture Fixture;
    GenericBuffer Root;
    GenericBuffer_AllocateVariable(&Root, WORLD_SPRITE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    if (TestAssetFixture_Construct(run, &Fixture)
        && TEST_CHECK_SUCCESS(run, TestRun_BuildRepositoryPath(run, "compile/out/asset", &Root))
        && TEST_CHECK_SUCCESS(run, AssetManager_AddSearchRoot(Fixture.Manager, Root._data)))
    {
        size_t FailedCount = 1U;
        TEST_CHECK_SUCCESS(run, AssetManager_ReadDefinitions(Fixture.Manager, &FailedCount));
        TEST_CHECK(run, FailedCount == 0U);
        for (size_t Index = 0U; Index < (sizeof(Names) / sizeof(Names[0])); Index++)
        {
            bool HasDefinition = false;
            TEST_CHECK(run, TEST_CHECK_SUCCESS(run, AssetManager_HasDefinition(Fixture.Manager,
                Fixture.Types.WorldSprite, (const unsigned char*)Names[Index], &HasDefinition)) && HasDefinition);
        }
    }
    Memory_Free(Root._data);
    TestAssetFixture_Deconstruct(run, &Fixture);
}


/* The shipped world sprites hold the look the test world's placements held before T26 (T06's settings): each file is
 * read as it ships and loaded over a code-built animation of the name it plays. */
static void TheShippedWorldSpritesKeepTheTestWorldsLook(TestRun* run)
{
    static const char* const Files[] = { "sprite", "tire_mark", "tree", "sign", "smoke", "glow", "neon" };
    WorldSpriteSettings Expected[sizeof(Files) / sizeof(Files[0])];
    for (size_t Index = 0U; Index < (sizeof(Files) / sizeof(Files[0])); Index++)
    {
        Expected[Index] = WorldSpriteSettings_CreateDefault();
    }
    Expected[0].PixelsPerUnit = 1280.0f;
    Expected[1].Orientation = WorldSpriteOrientation_Fixed;
    Expected[1].AlphaMode = WorldSpriteAlphaMode_Blend;
    Expected[1].IsDecal = true;
    Expected[2].Orientation = WorldSpriteOrientation_AxisBillboard;
    Expected[2].Pivot = (Vector2){ 0.5f, 0.0f };
    Expected[3].Orientation = WorldSpriteOrientation_Fixed;
    Expected[3].Pivot = (Vector2){ 0.5f, 0.0f };
    Expected[4].AlphaMode = WorldSpriteAlphaMode_Blend;
    Expected[5].AlphaMode = WorldSpriteAlphaMode_Additive;
    Expected[5].IsLit = false;
    Expected[5].Emission = 2.0f;
    Expected[6].Orientation = WorldSpriteOrientation_Fixed;
    Expected[6].IsLit = false;
    Expected[6].Emission = 3.0f;

    TestAssetFixture Fixture;
    GenericBuffer Path;
    GenericBuffer_AllocateVariable(&Path, WORLD_SPRITE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, WORLD_SPRITE_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    if (TestAssetFixture_Construct(run, &Fixture))
    {
        for (size_t Index = 0U; Index < (sizeof(Files) / sizeof(Files[0])); Index++)
        {
            char Relative[WORLD_SPRITE_TEST_TEXT_CAPACITY];
            char Name[WORLD_SPRITE_TEST_TEXT_CAPACITY];
            snprintf(Relative, sizeof(Relative), "compile/out/asset/world_sprites/test_world/%s.json", Files[Index]);
            snprintf(Name, sizeof(Name), "test_%s", Files[Index]);
            GenericBuffer_Clear(&Path);
            GenericBuffer_Clear(&Text);
            AssetUserID User = ASSET_USER_ID_INVALID;
            WorldSprite* Loaded = NULL;
            if (TEST_CHECK_SUCCESS(run, TestRun_BuildRepositoryPath(run, Relative, &Path))
                && TEST_CHECK_SUCCESS(run, FileSystem_ReadAllBytes(Path._data, &Text))
                && TEST_CHECK(run, GenericBuffer_NullTerminate(&Text))
                && TestAssets_AddSpriteAnimation(run, &Fixture, Name, NULL)
                && TestAssets_AddWorldSprite(run, &Fixture, (const char*)Text._data)
                && ((Loaded = LoadWorldSprite(run, &Fixture, Name, &User)) != NULL))
            {
                if (!TEST_CHECK(run, AreSettingsEqual(WorldSprite_GetSettings(Loaded), &Expected[Index])))
                {
                    printf("      (%s)\n", Name);
                }
            }
        }
    }
    Memory_Free(Text._data);
    Memory_Free(Path._data);
    TestAssetFixture_Deconstruct(run, &Fixture);
}


// Public functions.
void WorldSpriteTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "a_minimal_definition_gets_the_defaults", AMinimalDefinitionGetsTheDefaults },
        { "a_full_definition_reads_every_key", AFullDefinitionReadsEveryKey },
        { "each_rule_is_refused_with_a_clear_message", EachRuleIsRefusedWithAClearMessage },
        { "the_definition_shares_the_components_rows", TheDefinitionSharesTheComponentsRows },
        { "the_draw_distance_flag_is_explicit_and_round_trips", TheDrawDistanceFlagIsExplicitAndRoundTrips },
        { "loading_holds_the_animation_until_the_last_holder_goes", LoadingHoldsTheAnimationUntilTheLastHolderGoes },
        { "a_missing_animation_fails_the_load", AMissingAnimationFailsTheLoad },
        { "every_override_is_a_flag_next_to_its_value", EveryOverrideIsAFlagNextToItsValue },
        { "the_flags_choose_between_the_world_sprite_and_the_placement",
            TheFlagsChooseBetweenTheWorldSpriteAndThePlacement },
        { "the_draw_distance_comes_from_the_placement_the_world_sprite_or_the_kind",
            TheDrawDistanceComesFromThePlacementTheWorldSpriteOrTheKind },
        { "swapping_the_world_sprite_keeps_the_object_and_its_overrides",
            SwappingTheWorldSpriteKeepsTheObjectAndItsOverrides },
        { "the_shipped_world_sprites_are_read", TheShippedWorldSpritesAreRead },
        { "the_shipped_world_sprites_keep_the_test_worlds_look", TheShippedWorldSpritesKeepTheTestWorldsLook },
    };
    TestRun_RunSuite(run, "WorldSprite", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
