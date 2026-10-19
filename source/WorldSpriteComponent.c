#include <inttypes.h>
#include "WorldSpriteComponent.h"
#include "WorldInternal.h"
#include "wr/WRMemory.h"


// Macros.
/** The format version of the sprite component's property table. */
#define SPRITE_TABLE_VERSION 1U
/** The inspector's categories. */
#define CATEGORY_SPRITE ((const unsigned char*)u8"Sprite")
#define CATEGORY_LIGHTING ((const unsigned char*)u8"Lighting")
#define CATEGORY_DECAL ((const unsigned char*)u8"Decal")
/** The defaults that are not the type's zero. */
#define DEFAULT_ALPHA_CUTOFF 0.5f
#define DEFAULT_DECAL_OFFSET 0.01f


// Types.
/* The rows of the sprite component's table, in inspector order, so the setters and the hook reach their descriptors
 * without a search. */
typedef enum SpritePropertyEnum
{
    SpriteProperty_SpriteAnimation,
    SpriteProperty_Orientation,
    SpriteProperty_Pivot,
    SpriteProperty_PixelsPerUnit,
    SpriteProperty_FlipX,
    SpriteProperty_FlipY,
    SpriteProperty_AlphaMode,
    SpriteProperty_AlphaCutoff,
    SpriteProperty_IsLit,
    SpriteProperty_Emission,
    SpriteProperty_LightWrap,
    SpriteProperty_CastsShadows,
    SpriteProperty_IsDecal,
    SpriteProperty_DecalOffset,
    SpriteProperty_HasOutline,
    SpriteProperty_OmitPixelation,
    SpriteProperty_Count
} SpriteProperty;


// Fields.
static const PropertyEnumEntry OrientationEntries[] =
{
    { .Name = u8"billboard", .Value = WorldSpriteOrientation_Billboard },
    { .Name = u8"axis_billboard", .Value = WorldSpriteOrientation_AxisBillboard },
    { .Name = u8"fixed", .Value = WorldSpriteOrientation_Fixed },
};

static const PropertyEnumTable OrientationEnum =
{
    .Name = u8"sprite_orientation", .Entries = OrientationEntries,
    .EntryCount = sizeof(OrientationEntries) / sizeof(OrientationEntries[0])
};

static const PropertyEnumEntry AlphaModeEntries[] =
{
    { .Name = u8"cutout", .Value = WorldSpriteAlphaMode_Cutout },
    { .Name = u8"blend", .Value = WorldSpriteAlphaMode_Blend },
    { .Name = u8"additive", .Value = WorldSpriteAlphaMode_Additive },
};

static const PropertyEnumTable AlphaModeEnum =
{
    .Name = u8"sprite_alpha_mode", .Entries = AlphaModeEntries,
    .EntryCount = sizeof(AlphaModeEntries) / sizeof(AlphaModeEntries[0])
};

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
    [SpriteProperty_Orientation] =
    {
        .Key = u8"orientation", .WireId = 4U, .Type = PropertyType_Enum,
        PROPERTY_MEMBER(WorldSpriteComponent, _orientation), .Default.EnumValue = WorldSpriteOrientation_Billboard,
        .EnumTable = &OrientationEnum, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_SPRITE,
        .DisplayName = u8"Orientation",
        .Description = u8"\"billboard\" faces the camera; \"axis_billboard\" turns only around the object's own up axis "
            u8"to face it (trees, lamp posts); \"fixed\" keeps the object's rotation, facing its +Z (signs, decals).",
        .SinceVersion = 1U
    },
    [SpriteProperty_Pivot] =
    {
        .Key = u8"pivot", .WireId = 5U, .Type = PropertyType_Vector2, PROPERTY_MEMBER(WorldSpriteComponent, _pivot),
        .Default.Vector2Value = { 0.5f, 0.5f }, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_SPRITE,
        .DisplayName = u8"Pivot",
        .Description = u8"The point of the image at the object's position: [0, 0] its bottom left, [1, 1] its top right; "
            u8"[0.5, 0] for things standing on the ground.",
        .SinceVersion = 1U
    },
    [SpriteProperty_PixelsPerUnit] =
    {
        .Key = u8"pixels_per_unit", .WireId = 6U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldSpriteComponent, _pixelsPerUnit),
        .Default.FloatValue = WORLD_SPRITE_COMPONENT_DEFAULT_PIXELS_PER_UNIT, .Rules = PROPERTY_RULES_ABOVE(0.0),
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_SPRITE, .DisplayName = u8"Pixels per metre",
        .Description = u8"Image pixels per metre: the sprite's size is its frame's size divided by this, times the "
            u8"object's X and Y scale.",
        .SinceVersion = 1U
    },
    [SpriteProperty_FlipX] =
    {
        .Key = u8"flip_x", .WireId = 7U, .Type = PropertyType_Bool, PROPERTY_MEMBER(WorldSpriteComponent, _isFlippedX),
        .Default.BoolValue = false, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_SPRITE,
        .DisplayName = u8"Flip left-right", .Description = u8"Mirrors the image left to right, about the pivot.",
        .SinceVersion = 1U
    },
    [SpriteProperty_FlipY] =
    {
        .Key = u8"flip_y", .WireId = 8U, .Type = PropertyType_Bool, PROPERTY_MEMBER(WorldSpriteComponent, _isFlippedY),
        .Default.BoolValue = false, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_SPRITE,
        .DisplayName = u8"Flip upside down", .Description = u8"Mirrors the image top to bottom, about the pivot.",
        .SinceVersion = 1U
    },
    [SpriteProperty_AlphaMode] =
    {
        .Key = u8"alpha_mode", .WireId = 11U, .Type = PropertyType_Enum,
        PROPERTY_MEMBER(WorldSpriteComponent, _alphaMode), .Default.EnumValue = WorldSpriteAlphaMode_Cutout,
        .EnumTable = &AlphaModeEnum, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_SPRITE,
        .DisplayName = u8"Transparency",
        .Description = u8"\"cutout\": hard edges, opaque where drawn (pixel art; casts shadows); \"blend\": soft edges "
            u8"(smoke, stains); \"additive\": adds its light (glows).",
        .SinceVersion = 1U
    },
    [SpriteProperty_AlphaCutoff] =
    {
        .Key = u8"alpha_cutoff", .WireId = 12U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldSpriteComponent, _alphaCutoff), .Default.FloatValue = DEFAULT_ALPHA_CUTOFF,
        .Rules = PROPERTY_RULES_BETWEEN(0.0, 1.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_SPRITE,
        .DisplayName = u8"Cutout threshold",
        .Description = u8"With \"cutout\" transparency, texels less opaque than this are not drawn.", .SinceVersion = 1U
    },
    [SpriteProperty_IsLit] =
    {
        .Key = u8"is_lit", .WireId = 9U, .Type = PropertyType_Bool, PROPERTY_MEMBER(WorldSpriteComponent, _isLit),
        .Default.BoolValue = true, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_LIGHTING,
        .DisplayName = u8"Lit",
        .Description = u8"Lit by the sun, the moon and lamps like the models around it. Off: its colours glow as they "
            u8"are, by day and night (neon, fire).",
        .SinceVersion = 1U
    },
    [SpriteProperty_Emission] =
    {
        .Key = u8"emission", .WireId = 10U, .Type = PropertyType_Float, PROPERTY_MEMBER(WorldSpriteComponent, _emission),
        .Default.FloatValue = 1.0f, .Rules = PROPERTY_RULES_AT_LEAST(0.0), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_LIGHTING, .DisplayName = u8"Glow",
        .Description = u8"How bright an unlit sprite glows; 1 shows its colours as they are.", .SinceVersion = 1U
    },
    [SpriteProperty_LightWrap] =
    {
        .Key = u8"light_wrap", .WireId = 16U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldSpriteComponent, _lightWrap), .Default.FloatValue = 1.0f,
        .Rules = PROPERTY_RULES_BETWEEN(0.0, 1.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_LIGHTING,
        .DisplayName = u8"Light wrap",
        .Description = u8"Billboards only: how far light wraps around the sprite. 0 lights it as a flat card facing you, "
            u8"1 as a round thing, half lit from the side.",
        .SinceVersion = 1U
    },
    [SpriteProperty_CastsShadows] =
    {
        .Key = u8"casts_shadows", .WireId = 15U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldSpriteComponent, _castsShadows), .Default.BoolValue = true,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_LIGHTING, .DisplayName = u8"Casts shadows",
        .Description = u8"Cutout sprites only: casts a shadow (a billboard's shadow faces the light, so it does not turn "
            u8"with the camera). Decals and soft sprites never cast.",
        .SinceVersion = 1U
    },
    [SpriteProperty_IsDecal] =
    {
        .Key = u8"is_decal", .WireId = 13U, .Type = PropertyType_Bool, PROPERTY_MEMBER(WorldSpriteComponent, _isDecal),
        .Default.BoolValue = false, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_DECAL,
        .DisplayName = u8"Decal",
        .Description = u8"With the \"fixed\" orientation: lies on a surface (dirt, tire marks), drawn over it without "
            u8"shadow or outline.",
        .SinceVersion = 1U
    },
    [SpriteProperty_DecalOffset] =
    {
        .Key = u8"decal_offset", .WireId = 14U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldSpriteComponent, _decalOffset), .Default.FloatValue = DEFAULT_DECAL_OFFSET,
        .Rules = PROPERTY_RULES_AT_LEAST(0.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_DECAL,
        .DisplayName = u8"Lift",
        .Description = u8"Metres a decal is lifted off its surface so it does not flicker against it.",
        .SinceVersion = 1U
    },
    [SpriteProperty_HasOutline] =
    {
        .Key = u8"has_outline", .WireId = 2U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldSpriteComponent, _hasOutline), .Default.BoolValue = true, .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_SPRITE, .DisplayName = u8"Outline",
        .Description = u8"Draws a 1-pixel outline around the sprite's shape (cutout sprites).", .SinceVersion = 1U
    },
    [SpriteProperty_OmitPixelation] =
    {
        .Key = u8"omit_pixelation", .WireId = 3U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WorldSpriteComponent, _omitPixelation), .Default.BoolValue = false,
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_SPRITE, .DisplayName = u8"Crisp",
        .Description = u8"Draws the sprite at full resolution over the pixelated image (cutout sprites).",
        .SinceVersion = 1U
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

/* The typed setters' last step: the row changed, so the revision advances (and the hook runs). */
static Error FinishSet(WorldSpriteComponent* sprite, SpriteProperty property)
{
    WorldInternal_ComponentAfterSet(sprite, &SpriteProperties[property]);
    return Error_CreateSuccess();
}

/* Sets a float row after checking the value against its rules; nothing changes when it breaks them. */
static Error SetFloatProperty(WorldSpriteComponent* sprite, SpriteProperty property, float* field, float value)
{
    Error CheckResult = CheckProperty(property, PropertyValue_FromFloat(value));
    if (CheckResult.Code != ErrorCode_Success)
    {
        return CheckResult;
    }
    *field = value;
    return FinishSet(sprite, property);
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

Error WorldSpriteComponent_SetOrientation(WorldSpriteComponent* self, WorldSpriteOrientation orientation)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldSpriteComponent_SetOrientation: self must not be NULL.");
    }
    Error CheckResult = CheckProperty(SpriteProperty_Orientation, PropertyValue_FromEnum((int32_t)orientation));
    if (CheckResult.Code != ErrorCode_Success)
    {
        return CheckResult;
    }

    self->_orientation = orientation;
    return FinishSet(self, SpriteProperty_Orientation);
}

Error WorldSpriteComponent_SetPivot(WorldSpriteComponent* self, Vector2 pivot)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldSpriteComponent_SetPivot: self must not be NULL.");
    }
    Error CheckResult = CheckProperty(SpriteProperty_Pivot, PropertyValue_FromVector2(pivot));
    if (CheckResult.Code != ErrorCode_Success)
    {
        return CheckResult;
    }

    self->_pivot = pivot;
    return FinishSet(self, SpriteProperty_Pivot);
}

Error WorldSpriteComponent_SetPixelsPerUnit(WorldSpriteComponent* self, float pixelsPerUnit)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "WorldSpriteComponent_SetPixelsPerUnit: self must not be NULL.");
    }
    return SetFloatProperty(self, SpriteProperty_PixelsPerUnit, &self->_pixelsPerUnit, pixelsPerUnit);
}

Error WorldSpriteComponent_SetFlippedX(WorldSpriteComponent* self, bool isFlipped)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldSpriteComponent_SetFlippedX: self must not be NULL.");
    }
    self->_isFlippedX = isFlipped;
    return FinishSet(self, SpriteProperty_FlipX);
}

Error WorldSpriteComponent_SetFlippedY(WorldSpriteComponent* self, bool isFlipped)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldSpriteComponent_SetFlippedY: self must not be NULL.");
    }
    self->_isFlippedY = isFlipped;
    return FinishSet(self, SpriteProperty_FlipY);
}

Error WorldSpriteComponent_SetAlphaMode(WorldSpriteComponent* self, WorldSpriteAlphaMode alphaMode)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldSpriteComponent_SetAlphaMode: self must not be NULL.");
    }
    Error CheckResult = CheckProperty(SpriteProperty_AlphaMode, PropertyValue_FromEnum((int32_t)alphaMode));
    if (CheckResult.Code != ErrorCode_Success)
    {
        return CheckResult;
    }

    self->_alphaMode = alphaMode;
    return FinishSet(self, SpriteProperty_AlphaMode);
}

Error WorldSpriteComponent_SetAlphaCutoff(WorldSpriteComponent* self, float alphaCutoff)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldSpriteComponent_SetAlphaCutoff: self must not be NULL.");
    }
    return SetFloatProperty(self, SpriteProperty_AlphaCutoff, &self->_alphaCutoff, alphaCutoff);
}

Error WorldSpriteComponent_SetLit(WorldSpriteComponent* self, bool isLit)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldSpriteComponent_SetLit: self must not be NULL.");
    }
    self->_isLit = isLit;
    return FinishSet(self, SpriteProperty_IsLit);
}

Error WorldSpriteComponent_SetEmission(WorldSpriteComponent* self, float emission)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldSpriteComponent_SetEmission: self must not be NULL.");
    }
    return SetFloatProperty(self, SpriteProperty_Emission, &self->_emission, emission);
}

Error WorldSpriteComponent_SetLightWrap(WorldSpriteComponent* self, float lightWrap)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldSpriteComponent_SetLightWrap: self must not be NULL.");
    }
    return SetFloatProperty(self, SpriteProperty_LightWrap, &self->_lightWrap, lightWrap);
}

Error WorldSpriteComponent_SetCastsShadows(WorldSpriteComponent* self, bool castsShadows)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "WorldSpriteComponent_SetCastsShadows: self must not be NULL.");
    }
    self->_castsShadows = castsShadows;
    return FinishSet(self, SpriteProperty_CastsShadows);
}

Error WorldSpriteComponent_SetDecal(WorldSpriteComponent* self, bool isDecal)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldSpriteComponent_SetDecal: self must not be NULL.");
    }
    self->_isDecal = isDecal;
    return FinishSet(self, SpriteProperty_IsDecal);
}

Error WorldSpriteComponent_SetDecalOffset(WorldSpriteComponent* self, float decalOffset)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "WorldSpriteComponent_SetDecalOffset: self must not be NULL.");
    }
    return SetFloatProperty(self, SpriteProperty_DecalOffset, &self->_decalOffset, decalOffset);
}
