#include <inttypes.h>
#include "WorldSpriteComponent.h"
#include "WorldInternal.h"
#include "WorldSpriteInternal.h"
#include "AssetManager.h"
#include "wr/WRMemory.h"


// Macros.
/** The format version of the sprite component's property table. */
#define SPRITE_TABLE_VERSION 1U
/** An override flag's row: "overrides_" + its setting's key, a Bool (false: the world sprite's value applies), right
 *  before the value's row. @p name is how the flag's texts call the setting. */
#define OVERRIDE_ROW(setting, key, wireId, category, name) \
    { \
        .Key = u8"overrides_" key, .WireId = (wireId), .Type = PropertyType_Bool, \
        PROPERTY_MEMBER(WorldSpriteComponent, _overrides[setting]), .Default.BoolValue = false, \
        .Flags = PROPERTY_FLAGS_SETTING, .Category = (category), .DisplayName = u8"Override " name, \
        .Description = u8"Uses this object's " name u8" instead of its world sprite's.", .SinceVersion = 1U \
    }


// Types.
/* The rows of the sprite component's table, in inspector order, so the setters and the hook reach their descriptors
 * without a search. */
typedef enum SpritePropertyEnum
{
    SpriteProperty_WorldSprite,
    SpriteProperty_OverridesOrientation,
    SpriteProperty_Orientation,
    SpriteProperty_OverridesPivot,
    SpriteProperty_Pivot,
    SpriteProperty_OverridesPixelsPerUnit,
    SpriteProperty_PixelsPerUnit,
    SpriteProperty_OverridesFlipX,
    SpriteProperty_FlipX,
    SpriteProperty_OverridesFlipY,
    SpriteProperty_FlipY,
    SpriteProperty_OverridesAlphaMode,
    SpriteProperty_AlphaMode,
    SpriteProperty_OverridesAlphaCutoff,
    SpriteProperty_AlphaCutoff,
    SpriteProperty_OverridesIsLit,
    SpriteProperty_IsLit,
    SpriteProperty_OverridesEmission,
    SpriteProperty_Emission,
    SpriteProperty_OverridesLightWrap,
    SpriteProperty_LightWrap,
    SpriteProperty_OverridesCastsShadows,
    SpriteProperty_CastsShadows,
    SpriteProperty_OverridesIsDecal,
    SpriteProperty_IsDecal,
    SpriteProperty_OverridesDecalOffset,
    SpriteProperty_DecalOffset,
    SpriteProperty_OverridesHasOutline,
    SpriteProperty_HasOutline,
    SpriteProperty_OverridesOmitPixelation,
    SpriteProperty_OmitPixelation,
    SpriteProperty_OverridesHaze,
    SpriteProperty_Haze,
    SpriteProperty_OverridesDrawDistance,
    SpriteProperty_DrawDistance,
    SpriteProperty_DrawDistanceMultiplier,
    SpriteProperty_OverridesIsDrawDistanceFixed,
    SpriteProperty_IsDrawDistanceFixed,
    SpriteProperty_OverridesShadowDistance,
    SpriteProperty_ShadowDistance,
    SpriteProperty_ShadowDistanceMultiplier,
    SpriteProperty_OverridesIsShadowDistanceFixed,
    SpriteProperty_IsShadowDistanceFixed,
    SpriteProperty_OverridesIsShadowOnly,
    SpriteProperty_IsShadowOnly,
    SpriteProperty_Count
} SpriteProperty;


// Fields.
/* Every property, in inspector order: each override flag sits right before the value it switches on, and the values
 * share their rows with the world sprite's tables (WorldSpriteInternal.h). The wire ids are the save schema: never
 * renumber or reuse one; a removed property's id goes into the table's retired list. They do not follow the rows'
 * order: 1 is world_sprite, 2 to 16 the placement's own values and 17 to 31 their override flags, the haze's pair is 37
 * and 38 (the haze's id the world sprite's render table's), and the shadows' rows are 39 to 45 (their values' ids the
 * render table's too). The next free wire id is 46. */
static const PropertyDescriptor SpriteProperties[SpriteProperty_Count] =
{
    [SpriteProperty_WorldSprite] =
    {
        .Key = u8"world_sprite", .WireId = 1U, .Type = PropertyType_AssetName,
        PROPERTY_MEMBER(WorldSpriteComponent, _worldSprite), .Default.StringValue = NULL,
        .Rules = PROPERTY_RULES_STRING(WORLD_SPRITE_COMPONENT_NAME_MAX_LENGTH, true), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = WORLD_SPRITE_CATEGORY_SPRITE, .DisplayName = u8"World sprite",
        .Description = u8"The world sprite to show (its animation, shape, look and draw distance); none shows nothing.",
        .SinceVersion = 1U
    },
    [SpriteProperty_OverridesOrientation] = OVERRIDE_ROW(WorldSpriteSetting_Orientation, "orientation", 17U,
        WORLD_SPRITE_CATEGORY_SPRITE, "orientation"),
    [SpriteProperty_Orientation] =
    {
        WORLD_SPRITE_ROW_ORIENTATION, .WireId = 4U, PROPERTY_MEMBER(WorldSpriteComponent, _orientation),
        .SinceVersion = 1U
    },
    [SpriteProperty_OverridesPivot] = OVERRIDE_ROW(WorldSpriteSetting_Pivot, "pivot", 18U,
        WORLD_SPRITE_CATEGORY_SPRITE, "pivot"),
    [SpriteProperty_Pivot] =
    {
        WORLD_SPRITE_ROW_PIVOT, .WireId = 5U, PROPERTY_MEMBER(WorldSpriteComponent, _pivot), .SinceVersion = 1U
    },
    [SpriteProperty_OverridesPixelsPerUnit] = OVERRIDE_ROW(WorldSpriteSetting_PixelsPerUnit, "pixels_per_unit", 19U,
        WORLD_SPRITE_CATEGORY_SPRITE, "pixels per metre"),
    [SpriteProperty_PixelsPerUnit] =
    {
        WORLD_SPRITE_ROW_PIXELS_PER_UNIT, .WireId = 6U, PROPERTY_MEMBER(WorldSpriteComponent, _pixelsPerUnit),
        .SinceVersion = 1U
    },
    [SpriteProperty_OverridesFlipX] = OVERRIDE_ROW(WorldSpriteSetting_FlipX, "flip_x", 20U,
        WORLD_SPRITE_CATEGORY_SPRITE, "left-right flip"),
    [SpriteProperty_FlipX] =
    {
        WORLD_SPRITE_ROW_FLIP_X, .WireId = 7U, PROPERTY_MEMBER(WorldSpriteComponent, _isFlippedX), .SinceVersion = 1U
    },
    [SpriteProperty_OverridesFlipY] = OVERRIDE_ROW(WorldSpriteSetting_FlipY, "flip_y", 21U,
        WORLD_SPRITE_CATEGORY_SPRITE, "upside-down flip"),
    [SpriteProperty_FlipY] =
    {
        WORLD_SPRITE_ROW_FLIP_Y, .WireId = 8U, PROPERTY_MEMBER(WorldSpriteComponent, _isFlippedY), .SinceVersion = 1U
    },
    [SpriteProperty_OverridesAlphaMode] = OVERRIDE_ROW(WorldSpriteSetting_AlphaMode, "alpha_mode", 22U,
        WORLD_SPRITE_CATEGORY_SPRITE, "transparency"),
    [SpriteProperty_AlphaMode] =
    {
        WORLD_SPRITE_ROW_ALPHA_MODE, .WireId = 11U, PROPERTY_MEMBER(WorldSpriteComponent, _alphaMode),
        .SinceVersion = 1U
    },
    [SpriteProperty_OverridesAlphaCutoff] = OVERRIDE_ROW(WorldSpriteSetting_AlphaCutoff, "alpha_cutoff", 23U,
        WORLD_SPRITE_CATEGORY_SPRITE, "cutout threshold"),
    [SpriteProperty_AlphaCutoff] =
    {
        WORLD_SPRITE_ROW_ALPHA_CUTOFF, .WireId = 12U, PROPERTY_MEMBER(WorldSpriteComponent, _alphaCutoff),
        .SinceVersion = 1U
    },
    [SpriteProperty_OverridesIsLit] = OVERRIDE_ROW(WorldSpriteSetting_IsLit, "is_lit", 24U,
        WORLD_SPRITE_CATEGORY_LIGHTING, "lighting"),
    [SpriteProperty_IsLit] =
    {
        WORLD_SPRITE_ROW_IS_LIT, .WireId = 9U, PROPERTY_MEMBER(WorldSpriteComponent, _isLit), .SinceVersion = 1U
    },
    [SpriteProperty_OverridesEmission] = OVERRIDE_ROW(WorldSpriteSetting_Emission, "emission", 25U,
        WORLD_SPRITE_CATEGORY_LIGHTING, "glow"),
    [SpriteProperty_Emission] =
    {
        WORLD_SPRITE_ROW_EMISSION, .WireId = 10U, PROPERTY_MEMBER(WorldSpriteComponent, _emission), .SinceVersion = 1U
    },
    [SpriteProperty_OverridesLightWrap] = OVERRIDE_ROW(WorldSpriteSetting_LightWrap, "light_wrap", 26U,
        WORLD_SPRITE_CATEGORY_LIGHTING, "light wrap"),
    [SpriteProperty_LightWrap] =
    {
        WORLD_SPRITE_ROW_LIGHT_WRAP, .WireId = 16U, PROPERTY_MEMBER(WorldSpriteComponent, _lightWrap),
        .SinceVersion = 1U
    },
    [SpriteProperty_OverridesCastsShadows] = OVERRIDE_ROW(WorldSpriteSetting_CastsShadows, "casts_shadows", 27U,
        WORLD_SPRITE_CATEGORY_LIGHTING, "shadow casting"),
    [SpriteProperty_CastsShadows] =
    {
        WORLD_SPRITE_ROW_CASTS_SHADOWS, .WireId = 15U, PROPERTY_MEMBER(WorldSpriteComponent, _castsShadows),
        .SinceVersion = 1U
    },
    [SpriteProperty_OverridesIsDecal] = OVERRIDE_ROW(WorldSpriteSetting_IsDecal, "is_decal", 28U,
        WORLD_SPRITE_CATEGORY_DECAL, "decal setting"),
    [SpriteProperty_IsDecal] =
    {
        WORLD_SPRITE_ROW_IS_DECAL, .WireId = 13U, PROPERTY_MEMBER(WorldSpriteComponent, _isDecal), .SinceVersion = 1U
    },
    [SpriteProperty_OverridesDecalOffset] = OVERRIDE_ROW(WorldSpriteSetting_DecalOffset, "decal_offset", 29U,
        WORLD_SPRITE_CATEGORY_DECAL, "lift"),
    [SpriteProperty_DecalOffset] =
    {
        WORLD_SPRITE_ROW_DECAL_OFFSET, .WireId = 14U, PROPERTY_MEMBER(WorldSpriteComponent, _decalOffset),
        .SinceVersion = 1U
    },
    [SpriteProperty_OverridesHasOutline] = OVERRIDE_ROW(WorldSpriteSetting_HasOutline, "has_outline", 30U,
        WORLD_SPRITE_CATEGORY_SPRITE, "outline"),
    [SpriteProperty_HasOutline] =
    {
        WORLD_SPRITE_ROW_HAS_OUTLINE, .WireId = 2U, PROPERTY_MEMBER(WorldSpriteComponent, _hasOutline),
        .SinceVersion = 1U
    },
    [SpriteProperty_OverridesOmitPixelation] = OVERRIDE_ROW(WorldSpriteSetting_OmitPixelation, "omit_pixelation", 31U,
        WORLD_SPRITE_CATEGORY_SPRITE, "crisp setting"),
    [SpriteProperty_OmitPixelation] =
    {
        WORLD_SPRITE_ROW_OMIT_PIXELATION, .WireId = 3U, PROPERTY_MEMBER(WorldSpriteComponent, _omitPixelation),
        .SinceVersion = 1U
    },
    [SpriteProperty_OverridesHaze] = OVERRIDE_ROW(WorldSpriteSetting_Haze, "haze", 37U,
        WORLD_SPRITE_CATEGORY_LIGHTING, "haze"),
    [SpriteProperty_Haze] =
    {
        WORLD_SPRITE_ROW_HAZE, .WireId = 38U, PROPERTY_MEMBER(WorldSpriteComponent, _haze), .SinceVersion = 1U
    },
    [SpriteProperty_OverridesDrawDistance] = OVERRIDE_ROW(WorldSpriteSetting_DrawDistance, "draw_distance", 32U,
        WORLD_SPRITE_CATEGORY_DRAW_DISTANCE, "draw distance"),
    [SpriteProperty_DrawDistance] =
    {
        WORLD_SPRITE_ROW_DRAW_DISTANCE, .WireId = 33U, PROPERTY_MEMBER(WorldSpriteComponent, _drawDistance),
        .SinceVersion = 1U
    },
    [SpriteProperty_DrawDistanceMultiplier] =
    {
        .Key = u8"draw_distance_multiplier", .WireId = 34U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldSpriteComponent, _drawDistanceMultiplier), .Default.FloatValue = 1.0f,
        .Rules = PROPERTY_RULES_ABOVE(0.0), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = WORLD_SPRITE_CATEGORY_DRAW_DISTANCE, .DisplayName = u8"Draw distance multiplier",
        .Description = u8"Multiplies the draw distance in effect, the world sprite's or this object's: 2 is seen twice "
            u8"as far.",
        .SinceVersion = 1U
    },
    [SpriteProperty_OverridesIsDrawDistanceFixed] = OVERRIDE_ROW(WorldSpriteSetting_IsDrawDistanceFixed,
        "is_draw_distance_fixed", 35U, WORLD_SPRITE_CATEGORY_DRAW_DISTANCE, "fixed draw distance"),
    [SpriteProperty_IsDrawDistanceFixed] =
    {
        WORLD_SPRITE_ROW_IS_DRAW_DISTANCE_FIXED, .WireId = 36U,
        PROPERTY_MEMBER(WorldSpriteComponent, _isDrawDistanceFixed), .SinceVersion = 1U
    },
    [SpriteProperty_OverridesShadowDistance] = OVERRIDE_ROW(WorldSpriteSetting_ShadowDistance, "shadow_distance", 39U,
        WORLD_SPRITE_CATEGORY_SHADOWS, "shadow distance"),
    [SpriteProperty_ShadowDistance] =
    {
        WORLD_SPRITE_ROW_SHADOW_DISTANCE, .WireId = 40U, PROPERTY_MEMBER(WorldSpriteComponent, _shadowDistance),
        .SinceVersion = 1U
    },
    [SpriteProperty_ShadowDistanceMultiplier] =
    {
        .Key = u8"shadow_distance_multiplier", .WireId = 41U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WorldSpriteComponent, _shadowDistanceMultiplier), .Default.FloatValue = 1.0f,
        .Rules = PROPERTY_RULES_ABOVE(0.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = WORLD_SPRITE_CATEGORY_SHADOWS,
        .DisplayName = u8"Shadow distance multiplier",
        .Description = u8"Multiplies the shadow distance in effect, the world sprite's or this object's, or the draw "
            u8"distance when neither gives one: 0.5 casts half as far.",
        .SinceVersion = 1U
    },
    [SpriteProperty_OverridesIsShadowDistanceFixed] = OVERRIDE_ROW(WorldSpriteSetting_IsShadowDistanceFixed,
        "is_shadow_distance_fixed", 42U, WORLD_SPRITE_CATEGORY_SHADOWS, "fixed shadow distance"),
    [SpriteProperty_IsShadowDistanceFixed] =
    {
        WORLD_SPRITE_ROW_IS_SHADOW_DISTANCE_FIXED, .WireId = 43U,
        PROPERTY_MEMBER(WorldSpriteComponent, _isShadowDistanceFixed), .SinceVersion = 1U
    },
    [SpriteProperty_OverridesIsShadowOnly] = OVERRIDE_ROW(WorldSpriteSetting_IsShadowOnly, "is_shadow_only", 44U,
        WORLD_SPRITE_CATEGORY_SHADOWS, "shadow only setting"),
    [SpriteProperty_IsShadowOnly] =
    {
        WORLD_SPRITE_ROW_IS_SHADOW_ONLY, .WireId = 45U, PROPERTY_MEMBER(WorldSpriteComponent, _isShadowOnly),
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

/* Each setting's override flag row. */
static const SpriteProperty OverrideFlagRows[WorldSpriteSetting_Count] =
{
    [WorldSpriteSetting_Orientation] = SpriteProperty_OverridesOrientation,
    [WorldSpriteSetting_Pivot] = SpriteProperty_OverridesPivot,
    [WorldSpriteSetting_PixelsPerUnit] = SpriteProperty_OverridesPixelsPerUnit,
    [WorldSpriteSetting_FlipX] = SpriteProperty_OverridesFlipX,
    [WorldSpriteSetting_FlipY] = SpriteProperty_OverridesFlipY,
    [WorldSpriteSetting_AlphaMode] = SpriteProperty_OverridesAlphaMode,
    [WorldSpriteSetting_AlphaCutoff] = SpriteProperty_OverridesAlphaCutoff,
    [WorldSpriteSetting_IsLit] = SpriteProperty_OverridesIsLit,
    [WorldSpriteSetting_Emission] = SpriteProperty_OverridesEmission,
    [WorldSpriteSetting_LightWrap] = SpriteProperty_OverridesLightWrap,
    [WorldSpriteSetting_CastsShadows] = SpriteProperty_OverridesCastsShadows,
    [WorldSpriteSetting_IsDecal] = SpriteProperty_OverridesIsDecal,
    [WorldSpriteSetting_DecalOffset] = SpriteProperty_OverridesDecalOffset,
    [WorldSpriteSetting_HasOutline] = SpriteProperty_OverridesHasOutline,
    [WorldSpriteSetting_OmitPixelation] = SpriteProperty_OverridesOmitPixelation,
    [WorldSpriteSetting_Haze] = SpriteProperty_OverridesHaze,
    [WorldSpriteSetting_DrawDistance] = SpriteProperty_OverridesDrawDistance,
    [WorldSpriteSetting_IsDrawDistanceFixed] = SpriteProperty_OverridesIsDrawDistanceFixed,
    [WorldSpriteSetting_ShadowDistance] = SpriteProperty_OverridesShadowDistance,
    [WorldSpriteSetting_IsShadowDistanceFixed] = SpriteProperty_OverridesIsShadowDistanceFixed,
    [WorldSpriteSetting_IsShadowOnly] = SpriteProperty_OverridesIsShadowOnly,
};


// Static functions: binding.
/* Logs (once, now) that a sprite's world sprite could not be bound, and releases the error. */
static void ReportBindFailure(World* world, const WorldObject* object, const unsigned char* assetName, Error* error)
{
    Logger* Log = WorldInternal_GetLogger(world);
    if (Log != NULL)
    {
        const unsigned char* ObjectName = WorldObject_GetName(object);
        Error LogResult = Logger_LogWarningFormatted(Log,
            (const unsigned char*)u8"World: the sprite of object %" PRIu64 u8" (\"%s\") shows nothing: world sprite "
            u8"\"%s\" could not be loaded (%s).",
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

/* Backs the instance with the animation of the world sprite the component names, loaded under the world's asset user,
 * or empties it when the world has no asset manager, the component names none, or the load fails. A failed load is a
 * recoverable content problem: it is logged once, here, and the sprite stays empty (it draws nothing). The name is
 * remembered either way, so the same name is not bound (or reported) again. */
static void Bind(World* world, const WorldObject* object, WorldSpriteComponent* sprite)
{
    // TODO: A previous world sprite stays held under the world's user until the world is destroyed (holds are not
    // counted, so releasing it here could unload it under another sprite). If long-lived edited worlds pile up unused
    // world sprites, add a release pass that swaps to a fresh user holding only the bound ones.
    const unsigned char* AssetName = sprite->_worldSprite;
    AssetManager* Assets = WorldInternal_GetAssets(world);
    WorldSprite* Bound = NULL;
    if ((Assets != NULL) && (AssetName != NULL))
    {
        Error LoadResult = AssetManager_LoadWorldSprite(Assets, AssetName, WorldInternal_GetAssetUser(world), &Bound);
        if (LoadResult.Code != ErrorCode_Success)
        {
            ReportBindFailure(world, object, AssetName, &LoadResult);
            Bound = NULL;
        }
    }

    Error BindResult = BindInstance(sprite, (Bound != NULL) ? WorldSprite_GetAnimation(Bound) : NULL);
    if (BindResult.Code != ErrorCode_Success)
    {
        ReportBindFailure(world, object, AssetName, &BindResult);
        Bound = NULL;
    }
    sprite->_boundWorldSprite = Bound;

    unsigned char* BoundName = WorldInternal_CloneString(AssetName);
    Memory_Free(sprite->_boundName);
    sprite->_boundName = BoundName;
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

    // Empty (no source) until the world binds the world sprite.
    Result = SpriteAnimationInstance_Construct1(&Sprite->_animationInstance);
    if (Result.Code != ErrorCode_Success)
    {
        Properties_Deconstruct(&SpriteTable, Sprite);
        return Result;
    }
    Sprite->_boundName = NULL;
    Sprite->_boundWorldSprite = NULL;
    return Error_CreateSuccess();
}

static void DeconstructSprite(WorldComponent* component)
{
    WorldSpriteComponent* Sprite = (WorldSpriteComponent*)component;
    Error InstanceResult = SpriteAnimationInstance_Deconstruct(&Sprite->_animationInstance);
    Error_Deconstruct(&InstanceResult); // best-effort; tears down the instance's embedded event
    Memory_Free(Sprite->_boundName);
    Sprite->_boundName = NULL;
    Sprite->_boundWorldSprite = NULL;
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
    bool MayHaveRenamed = (descriptor == NULL) || (descriptor == &SpriteProperties[SpriteProperty_WorldSprite]);
    if (MayHaveRenamed && !WorldInternal_AreStringsEqual(Sprite->_worldSprite, Sprite->_boundName))
    {
        Bind(world, object, Sprite);
    }
}


// Static functions: the setters.
/* The error of a setter called without a component. */
static Error CreateNullSelfError(const char* function)
{
    return Error_Construct4(ErrorCode_IllegalArgument, "%s: self must not be NULL.", function);
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

/* Sets a bool row. */
static Error SetBoolProperty(WorldSpriteComponent* sprite, SpriteProperty property, bool* field, bool value)
{
    *field = value;
    return FinishSet(sprite, property);
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


// Static functions: the settings in effect.
/* The shape's settings: the placement's own where it overrides them. */
static void ResolveShape(const WorldSpriteComponent* self, WorldSpriteSettings* settings)
{
    if (self->_overrides[WorldSpriteSetting_Orientation])
    {
        settings->Orientation = self->_orientation;
    }
    if (self->_overrides[WorldSpriteSetting_Pivot])
    {
        settings->Pivot = self->_pivot;
    }
    if (self->_overrides[WorldSpriteSetting_PixelsPerUnit])
    {
        settings->PixelsPerUnit = self->_pixelsPerUnit;
    }
    if (self->_overrides[WorldSpriteSetting_FlipX])
    {
        settings->IsFlippedX = self->_isFlippedX;
    }
    if (self->_overrides[WorldSpriteSetting_FlipY])
    {
        settings->IsFlippedY = self->_isFlippedY;
    }
}

/* The transparency and lighting settings: the placement's own where it overrides them. */
static void ResolveLook(const WorldSpriteComponent* self, WorldSpriteSettings* settings)
{
    if (self->_overrides[WorldSpriteSetting_AlphaMode])
    {
        settings->AlphaMode = self->_alphaMode;
    }
    if (self->_overrides[WorldSpriteSetting_AlphaCutoff])
    {
        settings->AlphaCutoff = self->_alphaCutoff;
    }
    if (self->_overrides[WorldSpriteSetting_IsLit])
    {
        settings->IsLit = self->_isLit;
    }
    if (self->_overrides[WorldSpriteSetting_Emission])
    {
        settings->Emission = self->_emission;
    }
    if (self->_overrides[WorldSpriteSetting_LightWrap])
    {
        settings->LightWrap = self->_lightWrap;
    }
}

/* The decal and render settings: the placement's own where it overrides them. */
static void ResolveDecalAndRender(const WorldSpriteComponent* self, WorldSpriteSettings* settings)
{
    if (self->_overrides[WorldSpriteSetting_IsDecal])
    {
        settings->IsDecal = self->_isDecal;
    }
    if (self->_overrides[WorldSpriteSetting_DecalOffset])
    {
        settings->DecalOffset = self->_decalOffset;
    }
    if (self->_overrides[WorldSpriteSetting_CastsShadows])
    {
        settings->Render.CastsShadows = self->_castsShadows;
    }
    if (self->_overrides[WorldSpriteSetting_HasOutline])
    {
        settings->Render.HasOutline = self->_hasOutline;
    }
    if (self->_overrides[WorldSpriteSetting_OmitPixelation])
    {
        settings->Render.OmitsPixelation = self->_omitPixelation;
    }
    if (self->_overrides[WorldSpriteSetting_Haze])
    {
        settings->Render.Haze = self->_haze;
    }
    if (self->_overrides[WorldSpriteSetting_IsShadowDistanceFixed])
    {
        settings->Render.IsShadowDistanceFixed = self->_isShadowDistanceFixed;
    }
    if (self->_overrides[WorldSpriteSetting_IsShadowOnly])
    {
        settings->Render.IsShadowOnly = self->_isShadowOnly;
    }
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

Error WorldSpriteComponent_SetWorldSprite(WorldSpriteComponent* self, const unsigned char* worldSprite)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldSpriteComponent_SetWorldSprite");
    }
    Error CheckResult = CheckProperty(SpriteProperty_WorldSprite, PropertyValue_FromAssetName(worldSprite));
    if (CheckResult.Code != ErrorCode_Success)
    {
        return CheckResult;
    }

    unsigned char* Clone = WorldInternal_CloneAssetName(worldSprite);
    Memory_Free(self->_worldSprite);
    self->_worldSprite = Clone;
    return FinishSet(self, SpriteProperty_WorldSprite);
}

bool WorldSpriteComponent_IsOverridden(const WorldSpriteComponent* self, WorldSpriteSetting setting)
{
    // A value outside the enum: nothing is overridden.
    return ((unsigned int)setting < (unsigned int)WorldSpriteSetting_Count) && self->_overrides[setting];
}

Error WorldSpriteComponent_SetOverridden(WorldSpriteComponent* self, WorldSpriteSetting setting, bool isOverridden)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldSpriteComponent_SetOverridden");
    }
    if ((unsigned int)setting >= (unsigned int)WorldSpriteSetting_Count)
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange,
            "WorldSpriteComponent_SetOverridden: %d is not a WorldSpriteSetting.", (int)setting);
    }
    return SetBoolProperty(self, OverrideFlagRows[setting], &self->_overrides[setting], isOverridden);
}

Error WorldSpriteComponent_SetOrientation(WorldSpriteComponent* self, WorldSpriteOrientation orientation)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldSpriteComponent_SetOrientation");
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
        return CreateNullSelfError("WorldSpriteComponent_SetPivot");
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
        return CreateNullSelfError("WorldSpriteComponent_SetPixelsPerUnit");
    }
    return SetFloatProperty(self, SpriteProperty_PixelsPerUnit, &self->_pixelsPerUnit, pixelsPerUnit);
}

Error WorldSpriteComponent_SetFlippedX(WorldSpriteComponent* self, bool isFlipped)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldSpriteComponent_SetFlippedX");
    }
    return SetBoolProperty(self, SpriteProperty_FlipX, &self->_isFlippedX, isFlipped);
}

Error WorldSpriteComponent_SetFlippedY(WorldSpriteComponent* self, bool isFlipped)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldSpriteComponent_SetFlippedY");
    }
    return SetBoolProperty(self, SpriteProperty_FlipY, &self->_isFlippedY, isFlipped);
}

Error WorldSpriteComponent_SetAlphaMode(WorldSpriteComponent* self, WorldSpriteAlphaMode alphaMode)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldSpriteComponent_SetAlphaMode");
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
        return CreateNullSelfError("WorldSpriteComponent_SetAlphaCutoff");
    }
    return SetFloatProperty(self, SpriteProperty_AlphaCutoff, &self->_alphaCutoff, alphaCutoff);
}

Error WorldSpriteComponent_SetLit(WorldSpriteComponent* self, bool isLit)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldSpriteComponent_SetLit");
    }
    return SetBoolProperty(self, SpriteProperty_IsLit, &self->_isLit, isLit);
}

Error WorldSpriteComponent_SetEmission(WorldSpriteComponent* self, float emission)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldSpriteComponent_SetEmission");
    }
    return SetFloatProperty(self, SpriteProperty_Emission, &self->_emission, emission);
}

Error WorldSpriteComponent_SetLightWrap(WorldSpriteComponent* self, float lightWrap)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldSpriteComponent_SetLightWrap");
    }
    return SetFloatProperty(self, SpriteProperty_LightWrap, &self->_lightWrap, lightWrap);
}

Error WorldSpriteComponent_SetCastsShadows(WorldSpriteComponent* self, bool castsShadows)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldSpriteComponent_SetCastsShadows");
    }
    return SetBoolProperty(self, SpriteProperty_CastsShadows, &self->_castsShadows, castsShadows);
}

Error WorldSpriteComponent_SetDecal(WorldSpriteComponent* self, bool isDecal)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldSpriteComponent_SetDecal");
    }
    return SetBoolProperty(self, SpriteProperty_IsDecal, &self->_isDecal, isDecal);
}

Error WorldSpriteComponent_SetDecalOffset(WorldSpriteComponent* self, float decalOffset)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldSpriteComponent_SetDecalOffset");
    }
    return SetFloatProperty(self, SpriteProperty_DecalOffset, &self->_decalOffset, decalOffset);
}

Error WorldSpriteComponent_SetHasOutline(WorldSpriteComponent* self, bool hasOutline)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldSpriteComponent_SetHasOutline");
    }
    return SetBoolProperty(self, SpriteProperty_HasOutline, &self->_hasOutline, hasOutline);
}

Error WorldSpriteComponent_SetOmitsPixelation(WorldSpriteComponent* self, bool omitPixelation)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldSpriteComponent_SetOmitsPixelation");
    }
    return SetBoolProperty(self, SpriteProperty_OmitPixelation, &self->_omitPixelation, omitPixelation);
}

Error WorldSpriteComponent_SetHaze(WorldSpriteComponent* self, float haze)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldSpriteComponent_SetHaze");
    }
    return SetFloatProperty(self, SpriteProperty_Haze, &self->_haze, haze);
}

Error WorldSpriteComponent_SetDrawDistance(WorldSpriteComponent* self, float drawDistance)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldSpriteComponent_SetDrawDistance");
    }
    return SetFloatProperty(self, SpriteProperty_DrawDistance, &self->_drawDistance, drawDistance);
}

Error WorldSpriteComponent_SetDrawDistanceMultiplier(WorldSpriteComponent* self, float multiplier)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldSpriteComponent_SetDrawDistanceMultiplier");
    }
    return SetFloatProperty(self, SpriteProperty_DrawDistanceMultiplier, &self->_drawDistanceMultiplier, multiplier);
}

Error WorldSpriteComponent_SetIsDrawDistanceFixed(WorldSpriteComponent* self, bool isFixed)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldSpriteComponent_SetIsDrawDistanceFixed");
    }
    return SetBoolProperty(self, SpriteProperty_IsDrawDistanceFixed, &self->_isDrawDistanceFixed, isFixed);
}

Error WorldSpriteComponent_SetShadowDistance(WorldSpriteComponent* self, float shadowDistance)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldSpriteComponent_SetShadowDistance");
    }
    return SetFloatProperty(self, SpriteProperty_ShadowDistance, &self->_shadowDistance, shadowDistance);
}

Error WorldSpriteComponent_SetShadowDistanceMultiplier(WorldSpriteComponent* self, float multiplier)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldSpriteComponent_SetShadowDistanceMultiplier");
    }
    return SetFloatProperty(self, SpriteProperty_ShadowDistanceMultiplier, &self->_shadowDistanceMultiplier,
        multiplier);
}

Error WorldSpriteComponent_SetIsShadowDistanceFixed(WorldSpriteComponent* self, bool isFixed)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldSpriteComponent_SetIsShadowDistanceFixed");
    }
    return SetBoolProperty(self, SpriteProperty_IsShadowDistanceFixed, &self->_isShadowDistanceFixed, isFixed);
}

Error WorldSpriteComponent_SetIsShadowOnly(WorldSpriteComponent* self, bool isShadowOnly)
{
    if (self == NULL)
    {
        return CreateNullSelfError("WorldSpriteComponent_SetIsShadowOnly");
    }
    return SetBoolProperty(self, SpriteProperty_IsShadowOnly, &self->_isShadowOnly, isShadowOnly);
}

WorldSpriteSettings WorldSpriteComponent_ResolveSettings(const WorldSpriteComponent* self,
    const WorldSpriteSettings* spriteSettings)
{
    WorldSpriteSettings Settings = *spriteSettings;
    ResolveShape(self, &Settings);
    ResolveLook(self, &Settings);
    ResolveDecalAndRender(self, &Settings);

    if (self->_overrides[WorldSpriteSetting_DrawDistance])
    {
        Settings.DrawDistance = self->_drawDistance;
        Settings.HasDrawDistance = true;
    }
    if (!Settings.HasDrawDistance)
    {
        // The kind is the one in effect: a placement that turns its sprite into a decal gets the decals' distance.
        Settings.DrawDistance = WorldSpriteSettings_GetDefaultDrawDistance(&Settings);
        Settings.HasDrawDistance = true;
    }
    Settings.DrawDistance *= self->_drawDistanceMultiplier;
    if (self->_overrides[WorldSpriteSetting_IsDrawDistanceFixed])
    {
        Settings.IsDrawDistanceFixed = self->_isDrawDistanceFixed;
    }
    // The shadow distance in effect, after the draw distance (which stands for it without one), as a model's.
    if (self->_overrides[WorldSpriteSetting_ShadowDistance])
    {
        Settings.Render.ShadowDistance = self->_shadowDistance;
        Settings.Render.HasShadowDistance = true;
    }
    if (!Settings.Render.HasShadowDistance)
    {
        Settings.Render.ShadowDistance = Settings.DrawDistance;
    }
    Settings.Render.ShadowDistance *= self->_shadowDistanceMultiplier;
    return Settings;
}
