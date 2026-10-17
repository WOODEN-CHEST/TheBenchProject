#include "TestAssets.h"
#include "GameModel.h"
#include "SpriteAnimation.h"
#include "raylib/raymath.h"
#include "wr/WRCompile.h"
#include "wr/WRJSON.h"
#include <string.h>


// Types.
typedef struct TestAnimationDefinitionStruct
{
    AssetDefinition Base;
    TestAssetCounters* Counters;
} TestAnimationDefinition;

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
    TestAssetCounters* Counters;
} TestModelDefinition;

typedef struct TestModelLoadedStruct
{
    GameModel Model;
    TestAssetCounters* Counters;
} TestModelLoaded;


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
    GenericBuffer_AllocateVariable(&Loaded->Frames, 1, sizeof(SpriteAnimationFrame));
    SpriteAnimationFrame Frame = { 0 };
    Frame._areaInTexture = (Rectangle){ 0.0f, 0.0f, 1.0f, 1.0f };
    Error Result = GenericBuffer_AddLast(&Loaded->Frames, &Frame)
        ? SpriteAnimation_Construct1(&Loaded->Animation, &Loaded->Frames)
        : Error_Construct2(ErrorCode_InvalidOperation, "TestAnimation_LoadAsset: could not store the frame.");
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

static void TestModelLoaded_Destroy(LoadedAsset* self, AssetManager* manager)
{
    UNUSED(manager);
    TestModelLoaded* Loaded = self->DestroyContext;
    if (Loaded->Counters != NULL)
    {
        Loaded->Counters->UnloadCount++;
    }
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
    Loaded->Model._rayModel.transform = MatrixIdentity();
    Loaded->Model._localBounds = Definition->Bounds;
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

bool TestAssets_AddSpriteAnimation(TestRun* run, TestAssetFixture* fixture, const char* name,
    TestAssetCounters* counters)
{
    TestAnimationDefinition* Definition = Memory_Allocate(sizeof(TestAnimationDefinition));
    Definition->Base.VTable = &TestAnimationVTable;
    Definition->Base.Type = fixture->Types.SpriteAnimation;
    Definition->Base.Name = CloneName(name);
    Definition->Counters = counters;
    return TEST_CHECK_SUCCESS(run, AssetManager_SetDefinition(fixture->Manager, &Definition->Base));
}

bool TestAssets_AddModel(TestRun* run, TestAssetFixture* fixture, const char* name, BoundingBox bounds,
    TestAssetCounters* counters)
{
    TestModelDefinition* Definition = Memory_Allocate(sizeof(TestModelDefinition));
    Definition->Base.VTable = &TestModelVTable;
    Definition->Base.Type = fixture->Types.Model;
    Definition->Base.Name = CloneName(name);
    Definition->Bounds = bounds;
    Definition->Counters = counters;
    return TEST_CHECK_SUCCESS(run, AssetManager_SetDefinition(fixture->Manager, &Definition->Base));
}
