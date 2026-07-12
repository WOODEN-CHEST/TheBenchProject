#include "TextComponentResolver.h"
#include "GameFont.h"
#include "SpriteAnimation.h"


// Macros.
// Growth granularity of the sprite-animation instance pool.
#define INSTANCE_POOL_SECTION_CAPACITY ((size_t)16)


// Static functions.
// Keeps the first failure in *first and releases every later error so none leak.
static void AccumulateError(Error* first, Error incoming)
{
    if (incoming.Code == ErrorCode_Success)
    {
        Error_Deconstruct(&incoming);
        return;
    }
    if (first->Code == ErrorCode_Success)
    {
        *first = incoming;
    }
    else
    {
        Error_Deconstruct(&incoming);
    }
}

// Pool lifecycle hook: tear down a sprite-animation instance when the pool is deconstructed.
static Error DeconstructInstanceCallback(void* object, const UserData* userData)
{
    (void)userData;
    return SpriteAnimationInstance_Deconstruct(object);
}

static void ResolveStringComponent(StringComponent* component, AssetManager* assetManager, AssetUserID user, Error* first)
{
    if (component->_fontName == NULL)
    {
        return;
    }

    GameFont* Loaded = NULL;
    Error LoadResult = AssetManager_LoadFont(assetManager, component->_fontName, user, &Loaded);
    if (LoadResult.Code != ErrorCode_Success)
    {
        AccumulateError(first, LoadResult);
        return;
    }
    AccumulateError(first, StringComponent_SetFont(component, *Loaded));
}

static void ResolveSpriteComponent(SpriteComponent* component, AssetManager* assetManager, AssetUserID user,
    ObjectPool* instancePool, Error* first)
{
    // Only bind sprites that name an animation and are not already bound (avoids leaking a prior instance).
    if ((component->_animationName == NULL) || (component->_animationInstance != NULL))
    {
        return;
    }

    SpriteAnimation* Animation = NULL;
    Error LoadResult = AssetManager_LoadSpriteAnimation(assetManager, component->_animationName, user, &Animation);
    if (LoadResult.Code != ErrorCode_Success)
    {
        AccumulateError(first, LoadResult);
        return;
    }

    void* Slot = NULL;
    Error BorrowResult = ObjectPool_GetNewObject(instancePool, &Slot);
    if (BorrowResult.Code != ErrorCode_Success)
    {
        AccumulateError(first, BorrowResult);
        return;
    }

    SpriteAnimationInstance* Instance = Slot;
    Error CreateResult = SpriteAnimation_CreateInstance(Animation, Instance);
    if (CreateResult.Code != ErrorCode_Success)
    {
        AccumulateError(first, CreateResult);
        Error DisposeResult = ObjectPool_DisposeObject(instancePool, Slot);
        Error_Deconstruct(&DisposeResult);
        return;
    }

    AccumulateError(first, SpriteComponent_SetAnimationInstance(component, Instance));
}

static void ResolveComponent(TextComponent* component, AssetManager* assetManager, AssetUserID user,
    ObjectPool* instancePool, Error* first)
{
    switch (component->Type)
    {
        case TextComponentType_String:
            ResolveStringComponent((StringComponent*)component, assetManager, user, first);
            break;
        case TextComponentType_Sprite:
            ResolveSpriteComponent((SpriteComponent*)component, assetManager, user, instancePool, first);
            break;
        case TextComponentType_Empty:
        default:
            break;
    }

    size_t ChildCount = TextComponent_GetSubComponentCount(component);
    for (size_t Index = 0; Index < ChildCount; Index++)
    {
        TextComponent* Child = TextComponent_GetSubComponentAt(component, Index);
        if (Child != NULL)
        {
            ResolveComponent(Child, assetManager, user, instancePool, first);
        }
    }
}


// Public functions.
Error TextComponentResolver_ConstructInstancePool(ObjectPool* instancePool)
{
    if (instancePool == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextComponentResolver_ConstructInstancePool: instancePool must not be NULL.");
    }

    ObjectPoolLifecycle Lifecycle =
    {
        .ConstructObject = NULL,
        .ResetObject = NULL,
        .DeconstructObject = DeconstructInstanceCallback
    };
    return ObjectPool_Construct2(instancePool, sizeof(SpriteAnimationInstance),
        INSTANCE_POOL_SECTION_CAPACITY, Lifecycle, NULL);
}

Error TextComponentResolver_ResolveTree(TextComponent* root, AssetManager* assetManager, AssetUserID user,
    ObjectPool* instancePool)
{
    if ((root == NULL) || (assetManager == NULL) || (instancePool == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextComponentResolver_ResolveTree: root, assetManager and instancePool must not be NULL.");
    }

    Error First = Error_CreateSuccess();
    ResolveComponent(root, assetManager, user, instancePool, &First);
    return First;
}
