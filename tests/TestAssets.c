#include "TestAssets.h"
#include "GameModel.h"
#include "GameFont.h"
#include "GameTexture.h"
#include "SpriteAnimation.h"
#include "raylib/raymath.h"
#include "wr/WRCompile.h"
#include "wr/WRJSON.h"
#include <stdio.h>
#include <string.h>


// Types.
typedef struct TestAnimationDefinitionStruct
{
    AssetDefinition Base;
    size_t FrameCount;
    double FPS;
    bool IsPlaying;
    TestAssetCounters* Counters;
} TestAnimationDefinition;

typedef struct TestFontDefinitionStruct
{
    AssetDefinition Base;
    int Advance;
    Vector2 ShadowOffset;
    TestAssetCounters* Counters;
} TestFontDefinition;

typedef struct TestFontLoadedStruct
{
    GameFont Font;
    TestAssetCounters* Counters;
} TestFontLoaded;

typedef struct TestAnimationLoadedStruct
{
    SpriteAnimation Animation;
    GenericBuffer Frames;
    TestAssetCounters* Counters;
} TestAnimationLoaded;

typedef struct TestModelDefinitionStruct
{
    AssetDefinition Base;
    BoundingBox Bounds;
    int MaterialCount;
    Matrix Transform;
    TestAssetCounters* Counters;
} TestModelDefinition;

typedef struct TestModelLoadedStruct
{
    GameModel Model;
    TestAssetCounters* Counters;
} TestModelLoaded;

typedef struct TestTextureDefinitionStruct
{
    AssetDefinition Base;
    unsigned int TextureId;
    TestAssetCounters* Counters;
} TestTextureDefinition;

typedef struct TestTextureLoadedStruct
{
    GameTexture Texture;
    TestAssetCounters* Counters;
} TestTextureLoaded;


// Static functions.
static unsigned char* CloneName(const char* name)
{
    size_t Length = strlen(name);
    unsigned char* Clone = Memory_Allocate(Length + 1);
    Memory_Copy(name, Clone, Length + 1);
    return Clone;
}

static void TestDefinition_Destroy(void* self)
{
    AssetDefinition* Definition = self;
    Memory_Free(Definition->Name);
    Memory_Free(Definition);
}

static void TestAnimationLoaded_Destroy(LoadedAsset* self, AssetManager* manager)
{
    UNUSED(manager);
    TestAnimationLoaded* Loaded = self->DestroyContext;
    if (Loaded->Counters != NULL)
    {
        Loaded->Counters->UnloadCount++;
    }
    Error Result = SpriteAnimation_Deconstruct(&Loaded->Animation);
    Error_Deconstruct(&Result);
    Memory_Free(Loaded->Frames._data);
    Memory_Free(Loaded);
}

static const LoadedAssetVTable TestAnimationLoadedVTable = { TestAnimationLoaded_Destroy };

static Error TestAnimation_LoadAsset(void* self, AssetManager* manager, AssetUserID dependencyUser,
    LoadedAsset* outLoaded)
{
    UNUSED(manager);
    UNUSED(dependencyUser);
    TestAnimationDefinition* Definition = self;

    TestAnimationLoaded* Loaded = Memory_Allocate(sizeof(TestAnimationLoaded));
    Memory_Zero(Loaded, sizeof(*Loaded));
    GenericBuffer_AllocateVariable(&Loaded->Frames, Definition->FrameCount, sizeof(SpriteAnimationFrame));
    SpriteAnimationFrame Frame = { 0 };
    Frame._areaInTexture = (Rectangle){ 0.0f, 0.0f, 1.0f, 1.0f };
    Error Result = Error_CreateSuccess();
    for (size_t Index = 0; (Index < Definition->FrameCount) && (Result.Code == ErrorCode_Success); Index++)
    {
        if (!GenericBuffer_AddLast(&Loaded->Frames, &Frame))
        {
            Result = Error_Construct2(ErrorCode_InvalidOperation, "TestAnimation_LoadAsset: could not store a frame.");
        }
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = Definition->IsPlaying
            ? SpriteAnimation_Construct2(&Loaded->Animation, &Loaded->Frames, Definition->FPS, 1, true, true)
            : SpriteAnimation_Construct1(&Loaded->Animation, &Loaded->Frames);
    }
    if (Result.Code != ErrorCode_Success)
    {
        Memory_Free(Loaded->Frames._data);
        Memory_Free(Loaded);
        return Result;
    }

    Loaded->Counters = Definition->Counters;
    if (Loaded->Counters != NULL)
    {
        Loaded->Counters->LoadCount++;
    }
    outLoaded->Asset = &Loaded->Animation;
    outLoaded->VTable = &TestAnimationLoadedVTable;
    outLoaded->DestroyContext = Loaded;
    return Error_CreateSuccess();
}

static const AssetDefinitionVTable TestAnimationVTable = { TestAnimation_LoadAsset, TestDefinition_Destroy };

static void TestFontLoaded_Destroy(LoadedAsset* self, AssetManager* manager)
{
    UNUSED(manager);
    TestFontLoaded* Loaded = self->DestroyContext;
    if (Loaded->Counters != NULL)
    {
        Loaded->Counters->UnloadCount++;
    }
    Memory_Free(Loaded->Font._rayFont.glyphs);
    Memory_Free(Loaded->Font._rayFont.recs);
    Memory_Free(Loaded);
}

static const LoadedAssetVTable TestFontLoadedVTable = { TestFontLoaded_Destroy };

// A monospace font with metrics only: measuring needs no texture, so it works without a window.
static Error TestFont_LoadAsset(void* self, AssetManager* manager, AssetUserID dependencyUser, LoadedAsset* outLoaded)
{
    UNUSED(manager);
    UNUSED(dependencyUser);
    TestFontDefinition* Definition = self;

    int GlyphCount = (TEST_FONT_LAST_CODEPOINT - TEST_FONT_FIRST_CODEPOINT) + 1;
    TestFontLoaded* Loaded = Memory_Allocate(sizeof(TestFontLoaded));
    Memory_Zero(Loaded, sizeof(*Loaded));
    GlyphInfo* Glyphs = Memory_Allocate((size_t)GlyphCount * sizeof(GlyphInfo));
    Rectangle* Recs = Memory_Allocate((size_t)GlyphCount * sizeof(Rectangle));
    Memory_Zero(Glyphs, (size_t)GlyphCount * sizeof(GlyphInfo));
    for (int Index = 0; Index < GlyphCount; Index++)
    {
        Glyphs[Index].value = TEST_FONT_FIRST_CODEPOINT + Index;
        Glyphs[Index].advanceX = Definition->Advance;
        Recs[Index] = (Rectangle){ (float)(Index * Definition->Advance), 0.0f, (float)Definition->Advance,
            (float)TEST_FONT_BASE_SIZE };
    }

    Font RayFont = { 0 };
    RayFont.baseSize = TEST_FONT_BASE_SIZE;
    RayFont.glyphCount = GlyphCount;
    RayFont.glyphPadding = 0;
    RayFont.recs = Recs;
    RayFont.glyphs = Glyphs;
    Loaded->Font = GameFont_Wrap(RayFont);
    GameFont_SetShadowOffset(&Loaded->Font, Definition->ShadowOffset);
    Loaded->Counters = Definition->Counters;
    if (Loaded->Counters != NULL)
    {
        Loaded->Counters->LoadCount++;
    }
    outLoaded->Asset = &Loaded->Font;
    outLoaded->VTable = &TestFontLoadedVTable;
    outLoaded->DestroyContext = Loaded;
    return Error_CreateSuccess();
}

static const AssetDefinitionVTable TestFontVTable = { TestFont_LoadAsset, TestDefinition_Destroy };

static void TestModelLoaded_Destroy(LoadedAsset* self, AssetManager* manager)
{
    UNUSED(manager);
    TestModelLoaded* Loaded = self->DestroyContext;
    if (Loaded->Counters != NULL)
    {
        Loaded->Counters->UnloadCount++;
    }
    Memory_Free(Loaded->Model._rayModel.materials);
    Memory_Free(Loaded);
}

static const LoadedAssetVTable TestModelLoadedVTable = { TestModelLoaded_Destroy };

static Error TestModel_LoadAsset(void* self, AssetManager* manager, AssetUserID dependencyUser, LoadedAsset* outLoaded)
{
    UNUSED(manager);
    UNUSED(dependencyUser);
    TestModelDefinition* Definition = self;

    TestModelLoaded* Loaded = Memory_Allocate(sizeof(TestModelLoaded));
    Memory_Zero(Loaded, sizeof(*Loaded));
    Loaded->Model._rayModel.transform = Definition->Transform;
    Loaded->Model._localBounds = Definition->Bounds;
    if (Definition->MaterialCount > 0)
    {
        // Materials only count here (nothing draws them); their maps stay NULL.
        size_t ByteCount = (size_t)Definition->MaterialCount * sizeof(Material);
        Loaded->Model._rayModel.materials = Memory_Allocate(ByteCount);
        Memory_Zero(Loaded->Model._rayModel.materials, ByteCount);
        Loaded->Model._rayModel.materialCount = Definition->MaterialCount;
    }
    Loaded->Counters = Definition->Counters;
    if (Loaded->Counters != NULL)
    {
        Loaded->Counters->LoadCount++;
    }
    outLoaded->Asset = &Loaded->Model;
    outLoaded->VTable = &TestModelLoadedVTable;
    outLoaded->DestroyContext = Loaded;
    return Error_CreateSuccess();
}

static const AssetDefinitionVTable TestModelVTable = { TestModel_LoadAsset, TestDefinition_Destroy };

static void TestTextureLoaded_Destroy(LoadedAsset* self, AssetManager* manager)
{
    UNUSED(manager);
    TestTextureLoaded* Loaded = self->DestroyContext;
    if (Loaded->Counters != NULL)
    {
        Loaded->Counters->UnloadCount++;
    }
    Memory_Free(Loaded);
}

static const LoadedAssetVTable TestTextureLoadedVTable = { TestTextureLoaded_Destroy };

// A texture that only names a GPU id: copying it needs no GL context.
static Error TestTexture_LoadAsset(void* self, AssetManager* manager, AssetUserID dependencyUser,
    LoadedAsset* outLoaded)
{
    UNUSED(manager);
    UNUSED(dependencyUser);
    TestTextureDefinition* Definition = self;

    TestTextureLoaded* Loaded = Memory_Allocate(sizeof(TestTextureLoaded));
    Memory_Zero(Loaded, sizeof(*Loaded));
    Loaded->Texture._rayTexture = (Texture2D){ .id = Definition->TextureId, .width = 16, .height = 16, .mipmaps = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    Loaded->Texture._isSrgb = true;
    Loaded->Counters = Definition->Counters;
    if (Loaded->Counters != NULL)
    {
        Loaded->Counters->LoadCount++;
    }
    outLoaded->Asset = &Loaded->Texture;
    outLoaded->VTable = &TestTextureLoadedVTable;
    outLoaded->DestroyContext = Loaded;
    return Error_CreateSuccess();
}

static const AssetDefinitionVTable TestTextureVTable = { TestTexture_LoadAsset, TestDefinition_Destroy };


// Public functions.
bool TestAssetFixture_Construct(TestRun* run, TestAssetFixture* self)
{
    Memory_Zero(self, sizeof(*self));
    return TEST_CHECK_SUCCESS(run, AssetManager_Construct1(NULL, &self->Manager))
        && TEST_CHECK_SUCCESS(run, AssetManager_CreateStandardAssetTypes(self->Manager, &self->Types, &self->Pool));
}

void TestAssetFixture_Deconstruct(TestRun* run, TestAssetFixture* self)
{
    if (self->Manager != NULL)
    {
        TEST_CHECK_SUCCESS(run, AssetManager_Deconstruct(self->Manager));
    }
    if (self->Pool != NULL)
    {
        TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(self->Pool));
    }
}

static bool AddAnimation(TestRun* run, TestAssetFixture* fixture, const char* name, size_t frameCount, double fps,
    bool isPlaying, TestAssetCounters* counters)
{
    TestAnimationDefinition* Definition = Memory_Allocate(sizeof(TestAnimationDefinition));
    Memory_Zero(Definition, sizeof(*Definition));
    Definition->Base.VTable = &TestAnimationVTable;
    Definition->Base.Type = fixture->Types.SpriteAnimation;
    Definition->Base.Name = CloneName(name);
    Definition->FrameCount = frameCount;
    Definition->FPS = fps;
    Definition->IsPlaying = isPlaying;
    Definition->Counters = counters;
    return TEST_CHECK_SUCCESS(run, AssetManager_SetDefinition(fixture->Manager, &Definition->Base));
}

bool TestAssets_AddSpriteAnimation(TestRun* run, TestAssetFixture* fixture, const char* name,
    TestAssetCounters* counters)
{
    return AddAnimation(run, fixture, name, 1U, 0.0, false, counters);
}

bool TestAssets_AddPlayingSpriteAnimation(TestRun* run, TestAssetFixture* fixture, const char* name, size_t frameCount,
    double fps, TestAssetCounters* counters)
{
    return AddAnimation(run, fixture, name, frameCount, fps, true, counters);
}

bool TestAssets_AddFontWithShadowOffset(TestRun* run, TestAssetFixture* fixture, const char* name, int advancePixels,
    Vector2 shadowOffset, TestAssetCounters* counters)
{
    TestFontDefinition* Definition = Memory_Allocate(sizeof(TestFontDefinition));
    Memory_Zero(Definition, sizeof(*Definition));
    Definition->Base.VTable = &TestFontVTable;
    Definition->Base.Type = fixture->Types.Font;
    Definition->Base.Name = CloneName(name);
    Definition->Advance = advancePixels;
    Definition->ShadowOffset = shadowOffset;
    Definition->Counters = counters;
    return TEST_CHECK_SUCCESS(run, AssetManager_SetDefinition(fixture->Manager, &Definition->Base));
}

bool TestAssets_AddFont(TestRun* run, TestAssetFixture* fixture, const char* name, int advancePixels,
    TestAssetCounters* counters)
{
    Vector2 DefaultOffset = { .x = GAME_FONT_DEFAULT_SHADOW_OFFSET_X, .y = GAME_FONT_DEFAULT_SHADOW_OFFSET_Y };
    return TestAssets_AddFontWithShadowOffset(run, fixture, name, advancePixels, DefaultOffset, counters);
}

bool TestAssets_AddModel(TestRun* run, TestAssetFixture* fixture, const char* name, BoundingBox bounds,
    TestAssetCounters* counters)
{
    return TestAssets_AddModelWithMaterials(run, fixture, name, bounds, 0, MatrixIdentity(), counters);
}

bool TestAssets_AddModelWithMaterials(TestRun* run, TestAssetFixture* fixture, const char* name, BoundingBox bounds,
    int materialCount, Matrix transform, TestAssetCounters* counters)
{
    TestModelDefinition* Definition = Memory_Allocate(sizeof(TestModelDefinition));
    Memory_Zero(Definition, sizeof(*Definition));
    Definition->Base.VTable = &TestModelVTable;
    Definition->Base.Type = fixture->Types.Model;
    Definition->Base.Name = CloneName(name);
    Definition->Bounds = bounds;
    Definition->MaterialCount = materialCount;
    Definition->Transform = transform;
    Definition->Counters = counters;
    return TEST_CHECK_SUCCESS(run, AssetManager_SetDefinition(fixture->Manager, &Definition->Base));
}

bool TestAssets_AddTexture(TestRun* run, TestAssetFixture* fixture, const char* name, unsigned int textureId,
    TestAssetCounters* counters)
{
    TestTextureDefinition* Definition = Memory_Allocate(sizeof(TestTextureDefinition));
    Memory_Zero(Definition, sizeof(*Definition));
    Definition->Base.VTable = &TestTextureVTable;
    Definition->Base.Type = fixture->Types.Texture;
    Definition->Base.Name = CloneName(name);
    Definition->TextureId = textureId;
    Definition->Counters = counters;
    return TEST_CHECK_SUCCESS(run, AssetManager_SetDefinition(fixture->Manager, &Definition->Base));
}

/* Builds a definition of @p type from @p json with the type's real constructor and registers it. */
static bool AddFromJSON(TestRun* run, TestAssetFixture* fixture, AssetTypeID type, const char* json,
    const char* description)
{
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
    GenericBuffer_AppendString(&Bytes, (const unsigned char*)json);
    AssetDefinition* Definition = NULL;
    bool IsBuilt = TEST_CHECK_SUCCESS(run, AssetManager_BuildDefinition(fixture->Manager, type, &Bytes,
        (const unsigned char*)description, &Definition));
    Memory_Free(Bytes._data);
    return IsBuilt && TEST_CHECK_SUCCESS(run, AssetManager_SetDefinition(fixture->Manager, Definition));
}

bool TestAssets_AddWorldModel(TestRun* run, TestAssetFixture* fixture, const char* json)
{
    return AddFromJSON(run, fixture, fixture->Types.WorldModel, json, "test world model");
}

bool TestAssets_AddWorldModelOf(TestRun* run, TestAssetFixture* fixture, const char* name, const char* modelName)
{
    char Json[256];
    snprintf(Json, sizeof(Json), "{\"name\": \"%s\", \"lods\": [{\"model\": \"%s\", \"max_distance\": 600}]}",
        name, modelName);
    return TestAssets_AddWorldModel(run, fixture, Json);
}

bool TestAssets_AddWorldSprite(TestRun* run, TestAssetFixture* fixture, const char* json)
{
    return AddFromJSON(run, fixture, fixture->Types.WorldSprite, json, "test world sprite");
}

bool TestAssets_AddWorldSpriteOf(TestRun* run, TestAssetFixture* fixture, const char* name, const char* animationName)
{
    char Json[256];
    snprintf(Json, sizeof(Json), "{\"name\": \"%s\", \"sprite_animation\": \"%s\"}", name, animationName);
    return TestAssets_AddWorldSprite(run, fixture, Json);
}
