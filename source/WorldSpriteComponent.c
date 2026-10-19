#include <inttypes.h>
#include "WorldSpriteComponent.h"
#include "WorldInternal.h"
#include "wr/WRMemory.h"


// Macros.
/** The format version of the sprite component's property table. */
#define SPRITE_TABLE_VERSION 1U
/** The inspector's category. */
#define CATEGORY_SPRITE ((const unsigned char*)u8"Sprite")


// Types.
/* The rows of the sprite component's table, so the setters and the hook reach their descriptors without a search. */
typedef enum SpritePropertyEnum
{
    SpriteProperty_SpriteAnimation,
    SpriteProperty_HasOutline,
    SpriteProperty_OmitPixelation,
    SpriteProperty_Count
} SpriteProperty;


// Fields.
/* Every property, in inspector order. The wire ids are the save schema: never renumber or reuse one; a removed
 * property's id goes into the table's retired list. */
static const PropertyDescriptor SpriteProperties[SpriteProperty_Count] =
{
    [SpriteProperty_SpriteAnimation] =
    {
        .Key = u8"sprite_animation", .WireId = 1U, .Type = PropertyType_AssetName,
        PROPERTY_MEMBER(WorldSpriteComponent, _spriteAnimation), .Default.StringValue = NULL,
        .Rules = PROPERTY_RULES_STRING(WORLD_SPRITE_COMPONENT_NAME_MAX_LENGTH, true), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_SPRITE, .DisplayName = u8"Animation",
        .Description = u8"The sprite animation to play; none shows nothing.", .SinceVersion = 1U
    },
    [SpriteProperty_HasOutline] =
    {
        .Key = u8"has_outline", .WireId = 2U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldSpriteComponent, _hasOutline), .Default.BoolValue = true, .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_SPRITE, .DisplayName = u8"Outline",
        .Description = u8"Draws a 1-pixel outline around the sprite's shape.", .SinceVersion = 1U
    },
    [SpriteProperty_OmitPixelation] =
    {
        .Key = u8"omit_pixelation", .WireId = 3U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldSpriteComponent, _omitPixelation), .Default.BoolValue = false,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_SPRITE, .DisplayName = u8"Crisp",
        .Description = u8"Draws the sprite at full resolution over the pixelated image.", .SinceVersion = 1U
    },
};

static const PropertyTable SpriteTable =
{
    .Name = u8"sprite_component",
    .Version = SPRITE_TABLE_VERSION,
    .StructSize = sizeof(WorldSpriteComponent),
    .Descriptors = SpriteProperties,
    .DescriptorCount = SpriteProperty_Count,
    .RetiredWireIds = NULL,
    .RetiredWireIdCount = 0U,
    .AfterSet = WorldInternal_ComponentAfterSet,
    .Migrate = NULL
};


// Static functions: binding.
/* Logs (once, now) that a sprite's animation could not be bound, and releases the error. */
static void ReportBindFailure(World* world, const WorldObject* object, const unsigned char* assetName, Error* error)
{
    Logger* Log = WorldInternal_GetLogger(world);
    if (Log != NULL)
    {
        const unsigned char* ObjectName = WorldObject_GetName(object);
        Error LogResult = Logger_LogWarningFormatted(Log,
            (const unsigned char*)u8"World: the sprite of object %" PRIu64 u8" (\"%s\") has no animation: \"%s\" could "
            u8"not be loaded (%s).",
            WorldObject_GetId(object),
            (ObjectName != NULL) ? (const char*)ObjectName : "",
            (assetName != NULL) ? (const char*)assetName : "",
            (error->Message != NULL) ? (const char*)error->Message : "no details");
        Error_Deconstruct(&LogResult);
    }
    Error_Deconstruct(error);
}

/* Tears the instance down (and its event), then re-initializes it in place: backed by @p animation (from its defaults)
 * or empty when NULL. On failure the instance is left empty but constructed, so later calls and the deconstructor never
 * see a torn-down one. */
static Error BindInstance(WorldSpriteComponent* sprite, SpriteAnimation* animation)
{
    Error DeconstructResult = SpriteAnimationInstance_Deconstruct(&sprite->_animationInstance);
    if (DeconstructResult.Code != ErrorCode_Success)
    {
        return DeconstructResult;
    }
    if (animation == NULL)
    {
        return SpriteAnimationInstance_Construct1(&sprite->_animationInstance);
    }

    Error CreateResult = SpriteAnimation_CreateInstance(animation, &sprite->_animationInstance);
    if (CreateResult.Code != ErrorCode_Success)
    {
        // CreateInstance leaves the storage deconstructed on failure.
        Error EmptyResult = SpriteAnimationInstance_Construct1(&sprite->_animationInstance);
        Error_Deconstruct(&EmptyResult);
    }
    return CreateResult;
}

/* Backs the instance with the animation the sprite names, loaded under the world's asset user, or empties it when the
 * world has no asset manager, the sprite names none, or the load fails. A failed load is a recoverable content
 * problem: it is logged once, here, and the sprite stays empty (it draws nothing). The name is remembered either way,
 * so the same name is not bound (or reported) again. */
static void Bind(World* world, const WorldObject* object, WorldSpriteComponent* sprite)
{
    // TODO: A previous animation stays held under the world's user until the world is destroyed (holds are not
    // counted, so releasing it here could unload it under another sprite). If long-lived edited worlds pile up unused
    // animations, add a release pass that swaps to a fresh user holding only the bound animations.
    const unsigned char* AssetName = sprite->_spriteAnimation;
    AssetManager* Assets = WorldInternal_GetAssets(world);
    SpriteAnimation* Animation = NULL;
    if ((Assets != NULL) && (AssetName != NULL))
    {
        Error LoadResult = AssetManager_LoadSpriteAnimation(Assets, AssetName, WorldInternal_GetAssetUser(world),
            &Animation);
        if (LoadResult.Code != ErrorCode_Success)
        {
            ReportBindFailure(world, object, AssetName, &LoadResult);
            Animation = NULL;
        }
    }

    Error BindResult = BindInstance(sprite, Animation);
    if (BindResult.Code != ErrorCode_Success)
    {
        ReportBindFailure(world, object, AssetName, &BindResult);
    }

    unsigned char* BoundName = WorldInternal_CloneString(AssetName);
    Memory_Free(sprite->_boundAnimation);
    sprite->_boundAnimation = BoundName;
}


// Static functions: the registry's hooks.
static Error ConstructSprite(WorldComponent* component)
{
    WorldSpriteComponent* Sprite = (WorldSpriteComponent*)component;
    Error Result = Properties_Construct(&SpriteTable, Sprite);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    // Empty (no source) until the world binds the animation.
    Result = SpriteAnimationInstance_Construct1(&Sprite->_animationInstance);
    if (Result.Code != ErrorCode_Success)
    {
        Properties_Deconstruct(&SpriteTable, Sprite);
        return Result;
    }
    Sprite->_boundAnimation = NULL;
    return Error_CreateSuccess();
}

static void DeconstructSprite(WorldComponent* component)
{
    WorldSpriteComponent* Sprite = (WorldSpriteComponent*)component;
    Error InstanceResult = SpriteAnimationInstance_Deconstruct(&Sprite->_animationInstance);
    Error_Deconstruct(&InstanceResult); // best-effort; tears down the instance's embedded event
    Memory_Free(Sprite->_boundAnimation);
    Sprite->_boundAnimation = NULL;
    Properties_Deconstruct(&SpriteTable, Sprite);
}

static void AttachSprite(World* world, WorldObject* object, WorldComponent* component)
{
    Bind(world, object, (WorldSpriteComponent*)component);
}

/* Rebinds when the name changed: through its own row, or through a read or a copy that may have changed anything. */
static void OnSpritePropertySet(World* world, WorldObject* object, WorldComponent* component,
    const PropertyDescriptor* descriptor)
{
    WorldSpriteComponent* Sprite = (WorldSpriteComponent*)component;
    bool MayHaveRenamed = (descriptor == NULL) || (descriptor == &SpriteProperties[SpriteProperty_SpriteAnimation]);
    if (MayHaveRenamed && !WorldInternal_AreStringsEqual(Sprite->_spriteAnimation, Sprite->_boundAnimation))
    {
        Bind(world, object, Sprite);
    }
}

/* Checks @p value against one row's rules (so the setters' rules live in the table only). */
static Error CheckProperty(SpriteProperty property, PropertyValue value)
{
    return Properties_CheckValue(&SpriteTable, &SpriteProperties[property], &value);
}


// Fields: the registry entry (it refers to the functions above).
static const WorldComponentTypeInfo SpriteTypeInfo =
{
    .Type = WorldComponentType_Sprite,
    .Name = u8"sprite",
    .Size = sizeof(WorldSpriteComponent),
    .Table = &SpriteTable,
    .IsTransformDriver = false,
    .Construct = ConstructSprite,
    .Deconstruct = DeconstructSprite,
    .OnAttach = AttachSprite,
    .OnDetach = NULL,
    .OnPropertySet = OnSpritePropertySet
};


// Public functions.
const WorldComponentTypeInfo* WorldSpriteComponent_GetTypeInfo(void)
{
    return &SpriteTypeInfo;
}

const PropertyTable* WorldSpriteComponent_GetPropertyTable(void)
{
    return &SpriteTable;
}

Error WorldSpriteComponent_SetSpriteAnimation(WorldSpriteComponent* self, const unsigned char* spriteAnimation)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "WorldSpriteComponent_SetSpriteAnimation: self must not be NULL.");
    }
    Error CheckResult = CheckProperty(SpriteProperty_SpriteAnimation, PropertyValue_FromAssetName(spriteAnimation));
    if (CheckResult.Code != ErrorCode_Success)
    {
        return CheckResult;
    }

    unsigned char* Clone = WorldInternal_CloneString(spriteAnimation);
    Memory_Free(self->_spriteAnimation);
    self->_spriteAnimation = Clone;
    WorldInternal_ComponentAfterSet(self, &SpriteProperties[SpriteProperty_SpriteAnimation]);
    return Error_CreateSuccess();
}

Error WorldSpriteComponent_SetHasOutline(WorldSpriteComponent* self, bool hasOutline)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "WorldSpriteComponent_SetHasOutline: self must not be NULL.");
    }

    self->_hasOutline = hasOutline;
    WorldInternal_ComponentAfterSet(self, &SpriteProperties[SpriteProperty_HasOutline]);
    return Error_CreateSuccess();
}

Error WorldSpriteComponent_SetOmitsPixelation(WorldSpriteComponent* self, bool omitPixelation)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "WorldSpriteComponent_SetOmitsPixelation: self must not be NULL.");
    }

    self->_omitPixelation = omitPixelation;
    WorldInternal_ComponentAfterSet(self, &SpriteProperties[SpriteProperty_OmitPixelation]);
    return Error_CreateSuccess();
}
