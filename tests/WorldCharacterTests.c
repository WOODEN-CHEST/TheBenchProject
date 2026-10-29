#include "TestSuites.h"
#include "TestAssets.h"
#include "TestText.h"
#include "TestWorld.h"
#include "AssetManager.h"
#include "CharacterAsset.h"
#include "GameInput.h"
#include "GameMath.h"
#include "PlayerController.h"
#include "CharacterInternal.h"
#include "World.h"
#include "WorldCharacterComponent.h"
#include "WorldCharacterInternal.h"
#include "WorldCharacters.h"
#include "WorldColliderComponent.h"
#include "WorldCollision.h"
#include "WorldEnvironment.h"
#include "raylib/raymath.h"
#include "wr/WRMath.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>


// Macros.
/** The game's tick (480 Hz). */
#define TEST_DELTA (1.0f / 480.0f)
/** Ticks in a second. */
#define TICKS_PER_SECOND 480U
/** The default character's radius and height. */
#define TEST_RADIUS CHARACTER_DEFAULT_RADIUS
#define TEST_HEIGHT CHARACTER_DEFAULT_HEIGHT


// Types.
/* A world without assets, its collision world and its characters: every collider uses an own shape and every character
 * the default settings with its own overrides. */
typedef struct CharacterSceneStruct
{
    World World;
    KeyRegistry Keys;
    WorldCollision* Collision;
    WorldCharacters* Characters;
    bool IsBuilt;
} CharacterScene;

/* What a test box is. */
typedef enum TestBoxKindEnum
{
    TestBoxKind_Static,
    TestBoxKind_Kinematic,
    TestBoxKind_Pushable
} TestBoxKind;

/* What the events of a run counted, for one character. */
typedef struct EventCountsStruct
{
    size_t Counts[8];
    float LastLandingSpeed;
    uint64_t LastUsedId;
    float LastUsedDistance;
} EventCounts;


// Static functions: the scene.
static bool Scene_Construct(TestRun* run, CharacterScene* self)
{
    Memory_Zero(self, sizeof(*self));
    self->IsBuilt = TestWorld_Construct(run, &self->World, &self->Keys, NULL, NULL);
    return self->IsBuilt
        && TEST_CHECK_SUCCESS(run, WorldCollision_Create(&self->World, NULL, NULL, NULL, &self->Collision))
        && TEST_CHECK_SUCCESS(run, WorldCharacters_Create(&self->World, self->Collision, NULL, &self->Characters));
}

static void Scene_Deconstruct(TestRun* run, CharacterScene* self)
{
    WorldCharacters_Destroy(self->Characters);
    WorldCollision_Destroy(self->Collision);
    if (self->IsBuilt)
    {
        TestWorld_Deconstruct(run, &self->World, &self->Keys);
    }
}

static WorldObject* GetObject(CharacterScene* scene, uint64_t id)
{
    WorldObject* Object = NULL;
    World_TryGetObject(&scene->World, id, &Object);
    return Object;
}

/* A box of @p halfExtents at @p center of the scene (TestWorld_At: shifted in the far run) turned by @p rotation (Euler
 * XYZ, radians), with one own-shape collider. */
static uint64_t AddBox(TestRun* run, CharacterScene* scene, Vector3 center, Vector3 halfExtents, Vector3 rotation,
    TestBoxKind kind)
{
    WorldObject* Object = NULL;
    if (!TEST_CHECK_SUCCESS(run, World_CreateObject(&scene->World, (const unsigned char*)u8"box", &Object)))
    {
        return 0U;
    }
    uint64_t Id = WorldObject_GetId(Object);
    TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Object, TestWorld_At(center)));
    TEST_CHECK_SUCCESS(run, WorldObject_SetRotation(Object, rotation));
    TEST_CHECK_SUCCESS(run, WorldObject_SetMobility(Object, (kind == TestBoxKind_Static) ? WorldMobility_Static
        : WorldMobility_Movable));
    WorldComponent* Component = NULL;
    if (!TEST_CHECK_SUCCESS(run, World_AddComponent(&scene->World, Id, WorldComponentType_Collider, &Component)))
    {
        return Id;
    }
    WorldColliderComponent* Collider = WorldColliderComponent_FromComponent(Component);
    CollisionShape Shape = CollisionShape_Create(CollisionShapeType_Box);
    Shape.HalfExtents = halfExtents;
    TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetOwnShape(Collider, &Shape));
    TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetUsesOwnShape(Collider, true));
    TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetPushable(Collider, kind == TestBoxKind_Pushable));
    return Id;
}

/* A floor whose top is at y = 0. */
static uint64_t AddFloor(TestRun* run, CharacterScene* scene)
{
    return AddBox(run, scene, (Vector3){ 0.0f, -0.5f, 0.0f }, (Vector3){ 60.0f, 0.5f, 60.0f },
        (Vector3){ 0.0f, 0.0f, 0.0f }, TestBoxKind_Static);
}

static WorldCharacterComponent* GetCharacter(CharacterScene* scene, uint64_t id)
{
    WorldComponent* Component = NULL;
    return World_TryGetComponent(&scene->World, id, WorldComponentType_Character, &Component)
        ? WorldCharacterComponent_FromComponent(Component) : NULL;
}

/* A character (the default settings) with its feet placed at @p feet of the scene. Syncs the collision world first,
 * so the placement sees the scene. */
static uint64_t AddCharacter(TestRun* run, CharacterScene* scene, Vector3 feet)
{
    WorldObject* Object = NULL;
    if (!TEST_CHECK_SUCCESS(run, World_CreateObject(&scene->World, (const unsigned char*)u8"walker", &Object)))
    {
        return 0U;
    }
    uint64_t Id = WorldObject_GetId(Object);
    WorldComponent* Component = NULL;
    if (!TEST_CHECK_SUCCESS(run, World_AddComponent(&scene->World, Id, WorldComponentType_Character, &Component)))
    {
        return Id;
    }
    WorldCollision_Sync(scene->Collision);
    TEST_CHECK_SUCCESS(run, WorldCharacters_Teleport(scene->Characters, Id, TestWorld_At(feet), 0.0f));
    return Id;
}

static void Override(TestRun* run, CharacterScene* scene, uint64_t id, CharacterSetting setting, PropertyValue value)
{
    TEST_CHECK_SUCCESS(run, WorldCharacterComponent_OverrideSetting(GetCharacter(scene, id), setting, &value));
}

static void OverrideFloat(TestRun* run, CharacterScene* scene, uint64_t id, CharacterSetting setting, float value)
{
    Override(run, scene, id, setting, PropertyValue_FromFloat(value));
}

static void OverrideBool(TestRun* run, CharacterScene* scene, uint64_t id, CharacterSetting setting, bool value)
{
    Override(run, scene, id, setting, PropertyValue_FromBool(value));
}

static void SetInput(TestRun* run, CharacterScene* scene, uint64_t id, const CharacterInput* input)
{
    TEST_CHECK_SUCCESS(run, WorldCharacterComponent_SetInput(GetCharacter(scene, id), input));
}

/* An input walking along @p direction (world space). */
static CharacterInput CreateWalk(Vector3 direction)
{
    CharacterInput Input = CharacterInput_CreateDefault();
    Input.MoveDirection = direction;
    return Input;
}

/* Where a character's feet are in the scene. */
static Vector3 GetFeet(CharacterScene* scene, uint64_t id)
{
    return TestWorld_ToScene(WorldObject_GetWorldPosition(GetObject(scene, id)));
}

static const WorldCharacterState* GetState(CharacterScene* scene, uint64_t id)
{
    return WorldCharacterComponent_GetState(GetCharacter(scene, id));
}

/* Counts the queued events of @p id and clears the queue. */
static void TakeEvents(CharacterScene* scene, uint64_t id, EventCounts* counts)
{
    size_t Count = 0U;
    const WorldCharacterEvent* Events = WorldCharacters_GetEvents(scene->Characters, &Count);
    for (size_t Index = 0U; Index < Count; Index++)
    {
        if ((Events[Index].CharacterId != id) || ((size_t)Events[Index].Kind >= 8U))
        {
            continue;
        }
        counts->Counts[Events[Index].Kind]++;
        if (Events[Index].Kind == WorldCharacterEventKind_Landed)
        {
            counts->LastLandingSpeed = Events[Index].Value;
        }
        if (Events[Index].Kind == WorldCharacterEventKind_Used)
        {
            counts->LastUsedId = Events[Index].OtherId;
            counts->LastUsedDistance = Events[Index].Value;
        }
    }
    WorldCharacters_ClearEvents(scene->Characters);
}

/* One tick in the game's order: the characters (the drivers), the collision step, the world matrices. */
static void Tick(CharacterScene* scene)
{
    WorldCharacters_Update(scene->Characters, TEST_DELTA);
    WorldCollision_Step(scene->Collision, TEST_DELTA);
    World_UpdateTransforms(&scene->World);
}

static void TickFor(CharacterScene* scene, uint32_t ticks)
{
    for (uint32_t Index = 0U; Index < ticks; Index++)
    {
        Tick(scene);
    }
}

static bool IsNear(float a, float b, float tolerance)
{
    return fabsf(a - b) <= tolerance;
}


// Static functions: the asset and the settings.
static void TheTablesPassTheirChecksAndShareTheirRows(TestRun* run)
{
    const PropertyTable* Settings = CharacterSettings_GetPropertyTable();
    const PropertyTable* Component = WorldCharacterComponent_GetPropertyTable();
    TEST_CHECK_SUCCESS(run, PropertyTable_Check(Settings));
    TEST_CHECK_SUCCESS(run, PropertyTable_Check(Component));
    TEST_CHECK_SUCCESS(run, WorldComponentTypeInfo_Check(WorldCharacterComponent_GetTypeInfo()));
    TEST_CHECK(run, Settings->DescriptorCount == (size_t)CharacterSetting_Count);
    TEST_CHECK(run, Component->DescriptorCount == (1U + (2U * (size_t)CharacterSetting_Count)));
    if ((Settings->DescriptorCount != (size_t)CharacterSetting_Count)
        || (Component->DescriptorCount != (1U + (2U * (size_t)CharacterSetting_Count))))
    {
        return;
    }

    // Each setting: the component's flag ("overrides_" + the key) and its value row, the asset's row the same.
    for (size_t Index = 0U; Index < (size_t)CharacterSetting_Count; Index++)
    {
        const PropertyDescriptor* Asset = &Settings->Descriptors[Index];
        const PropertyDescriptor* Flag = &Component->Descriptors[1U + (2U * Index)];
        const PropertyDescriptor* Value = &Component->Descriptors[2U + (2U * Index)];
        TEST_CHECK(run, CharacterSettings_GetDescriptor((CharacterSetting)Index) == Asset);
        char FlagKey[96];
        snprintf(FlagKey, sizeof(FlagKey), "overrides_%s", (const char*)Asset->Key);
        TEST_CHECK(run, strcmp((const char*)Flag->Key, FlagKey) == 0);
        TEST_CHECK(run, Flag->Type == PropertyType_Bool);
        TEST_CHECK(run, strcmp((const char*)Value->Key, (const char*)Asset->Key) == 0);
        TEST_CHECK(run, (Value->Type == Asset->Type) && (Value->Size == Asset->Size));
        TEST_CHECK(run, memcmp(&Value->Default, &Asset->Default, sizeof(PropertyData)) == 0);
        TEST_CHECK(run, memcmp(&Value->Rules, &Asset->Rules, sizeof(PropertyRules)) == 0);
        TEST_CHECK(run, (Value->WireId == Asset->WireId) && (Value->WireId == CHARACTER_VALUE_WIRE_ID(Index))
            && (Flag->WireId == CHARACTER_FLAG_WIRE_ID(Index)));
    }

    // The default masks are the layers' own.
    TEST_CHECK(run, CHARACTER_DEFAULT_COLLIDES_WITH == CollisionLayer_GetDefaultMask(CollisionLayer_Character));
    TEST_CHECK(run, CHARACTER_DEFAULT_INTERACTION_LAYERS
        == (COLLISION_LAYER_MASK_ALL & ~COLLISION_LAYER_BIT(CollisionLayer_Trigger)));
    CharacterSettings Defaults = CharacterSettings_CreateDefault();
    TEST_CHECK_SUCCESS(run, CharacterSettings_CheckBody(&Defaults));
    TEST_CHECK(run, (Defaults.Radius == CHARACTER_DEFAULT_RADIUS) && (Defaults.Height == CHARACTER_DEFAULT_HEIGHT)
        && !Defaults.CanJump && !Defaults.CanFly && Defaults.CanCrouch);
}

static void EveryOverrideResolves(TestRun* run)
{
    CharacterScene Scene;
    if (!Scene_Construct(run, &Scene))
    {
        Scene_Deconstruct(run, &Scene);
        return;
    }
    uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f });
    WorldCharacterComponent* Character = GetCharacter(&Scene, Id);
    if (Character != NULL)
    {
        // A value other than the default for every setting, inside its rules.
        CharacterSettings Defaults = CharacterSettings_CreateDefault();
        for (size_t Index = 0U; Index < (size_t)CharacterSetting_Count; Index++)
        {
            const PropertyDescriptor* Row = CharacterSettings_GetDescriptor((CharacterSetting)Index);
            PropertyValue Value = { .Type = Row->Type, .Data = Row->Default };
            switch (Row->Type)
            {
                case PropertyType_Bool:
                    Value.Data.BoolValue = !Row->Default.BoolValue;
                    break;
                case PropertyType_EnumSet:
                    Value.Data.EnumSetValue = COLLISION_LAYER_BIT(CollisionLayer_World);
                    break;
                case PropertyType_Enum:
                    // The ground control's other value.
                    Value.Data.EnumValue = (Row->Default.EnumValue == 0) ? 1 : 0;
                    break;
                default:
                    // Half the default, or a little when the default is 0 (no snap, no boost, no air control).
                    Value.Data.FloatValue = (Row->Default.FloatValue > 0.0f) ? (Row->Default.FloatValue * 0.5f) : 0.5f;
                    break;
            }
            TEST_CHECK_SUCCESS(run, WorldCharacterComponent_OverrideSetting(Character, (CharacterSetting)Index,
                &Value));
            TEST_CHECK(run, WorldCharacterComponent_IsOverridden(Character, (CharacterSetting)Index));
        }
        CharacterSettings Resolved = WorldCharacterComponent_ResolveSettings(Character, &Defaults);
        for (size_t Index = 0U; Index < (size_t)CharacterSetting_Count; Index++)
        {
            const PropertyDescriptor* Row = CharacterSettings_GetDescriptor((CharacterSetting)Index);
            TEST_CHECK(run, memcmp((const unsigned char*)&Resolved + Row->Offset,
                (const unsigned char*)&Defaults + Row->Offset, Row->Size) != 0);
        }
        // Off again: the asset's value applies, the placement's own is kept.
        TEST_CHECK_SUCCESS(run, WorldCharacterComponent_SetOverridden(Character, CharacterSetting_WalkSpeed, false));
        Resolved = WorldCharacterComponent_ResolveSettings(Character, &Defaults);
        TEST_CHECK(run, Resolved.WalkSpeed == Defaults.WalkSpeed);
        TEST_CHECK(run, Character->_values.WalkSpeed == (Defaults.WalkSpeed * 0.5f));

        // A value breaking its rules changes nothing; a setting outside the enum is refused.
        PropertyValue Negative = PropertyValue_FromFloat(-1.0f);
        TEST_CHECK_ERROR(run, WorldCharacterComponent_OverrideSetting(Character, CharacterSetting_Radius, &Negative),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, Character->_values.Radius == (Defaults.Radius * 0.5f));
        TEST_CHECK_ERROR(run, WorldCharacterComponent_SetOverridden(Character, CharacterSetting_Count, true),
            ErrorCode_ArgumentOutOfRange);
        // Through the table, as scripts do.
        PropertyValue Speed = PropertyValue_FromFloat(2.5f);
        TEST_CHECK_SUCCESS(run, Properties_Set(WorldCharacterComponent_GetPropertyTable(), Character,
            (const unsigned char*)u8"walk_speed", &Speed));
        TEST_CHECK_SUCCESS(run, WorldCharacterComponent_SetOverridden(Character, CharacterSetting_WalkSpeed, true));
        TEST_CHECK(run, WorldCharacterComponent_GetSettings(Character).WalkSpeed == 2.5f);
    }
    Scene_Deconstruct(run, &Scene);
}

static void TheBodyRulesHoldAcrossSettings(TestRun* run)
{
    CharacterSettings Settings = CharacterSettings_CreateDefault();
    Settings.Height = 0.5f; // below twice the radius
    TEST_CHECK_ERROR(run, CharacterSettings_CheckBody(&Settings), ErrorCode_ArgumentOutOfRange);
    float Height = 0.0f;
    float CrouchHeight = 0.0f;
    CharacterSettings_GetBodyHeights(&Settings, &Height, &CrouchHeight);
    TEST_CHECK(run, (Height == 0.6f) && (CrouchHeight == 0.6f));

    Settings = CharacterSettings_CreateDefault();
    Settings.CrouchHeight = 2.0f; // above the height
    TEST_CHECK_ERROR(run, CharacterSettings_CheckBody(&Settings), ErrorCode_ArgumentOutOfRange);
    CharacterSettings_GetBodyHeights(&Settings, &Height, &CrouchHeight);
    TEST_CHECK(run, (Height == Settings.Height) && (CrouchHeight == Settings.Height));
}

static void TheInputIsCheckedAndNormalised(TestRun* run)
{
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f });
        WorldCharacterComponent* Character = GetCharacter(&Scene, Id);
        CharacterInput Input = CreateWalk((Vector3){ 3.0f, 0.0f, 4.0f });
        Input.LookPitch = 3.0f;
        Input.LookYaw = 7.0f;
        TEST_CHECK_SUCCESS(run, WorldCharacterComponent_SetInput(Character, &Input));
        const CharacterInput* Stored = WorldCharacterComponent_GetInput(Character);
        TEST_CHECK(run, IsNear(Vector3Length(Stored->MoveDirection), 1.0f, 1e-5f));
        TEST_CHECK(run, Stored->LookPitch == CHARACTER_MAX_LOOK_PITCH);
        TEST_CHECK(run, IsNear(Stored->LookYaw, 7.0f - (2.0f * PI), 1e-5f));

        Input.MoveDirection.x = NAN;
        TEST_CHECK_ERROR(run, WorldCharacterComponent_SetInput(Character, &Input), ErrorCode_ArgumentOutOfRange);
        Input = CharacterInput_CreateDefault();
        Input.Facing = (CharacterFacing)7;
        TEST_CHECK_ERROR(run, WorldCharacterComponent_SetInput(Character, &Input), ErrorCode_ArgumentOutOfRange);

        CharacterInput_SetLookDirection(&Input, (Vector3){ 1.0f, 1.0f, 0.0f });
        TEST_CHECK(run, IsNear(Input.LookYaw, 0.5f * PI, 1e-5f) && IsNear(Input.LookPitch, 0.25f * PI, 1e-5f));
        Vector3 Direction = CharacterInput_GetLookDirection(&Input);
        TEST_CHECK(run, IsNear(Direction.x, 0.70710678f, 1e-5f) && IsNear(Direction.y, 0.70710678f, 1e-5f));

        // Huge but finite moves are directions too (a float length would overflow to infinity and scale them to zero).
        Input = CreateWalk((Vector3){ 1e20f, 0.0f, 0.0f });
        TEST_CHECK_SUCCESS(run, WorldCharacterComponent_SetInput(Character, &Input));
        TEST_CHECK(run, IsNear(Stored->MoveDirection.x, 1.0f, 1e-6f) && (Stored->MoveDirection.y == 0.0f));
        Input = CreateWalk((Vector3){ FLT_MAX, -FLT_MAX, FLT_MAX });
        TEST_CHECK_SUCCESS(run, WorldCharacterComponent_SetInput(Character, &Input));
        TEST_CHECK(run, IsNear(Vector3Length(Stored->MoveDirection), 1.0f, 1e-5f) && (Stored->MoveDirection.y < 0.0f));
    }
    Scene_Deconstruct(run, &Scene);
}


// Static functions: walking.
static void ACharacterGetsACapsuleColliderAndStandsStill(TestRun* run)
{
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 1.0f, 0.0f, 2.0f });
        TickFor(&Scene, TICKS_PER_SECOND);
        WorldComponent* Component = NULL;
        TEST_CHECK(run, World_TryGetComponent(&Scene.World, Id, WorldComponentType_Collider, &Component));
        WorldColliderComponent* Collider = WorldColliderComponent_FromComponent(Component);
        if (Collider != NULL)
        {
            CollisionShape Shape = WorldColliderComponent_GetOwnShape(Collider);
            TEST_CHECK(run, WorldColliderComponent_UsesOwnShape(Collider) && (Shape.Type == CollisionShapeType_Capsule));
            TEST_CHECK(run, (Shape.Radius == TEST_RADIUS) && IsNear(Shape.HalfHeight, (0.5f * TEST_HEIGHT) - TEST_RADIUS,
                1e-6f) && IsNear(Shape.Center.y, 0.5f * TEST_HEIGHT, 1e-6f));
            TEST_CHECK(run, WorldColliderComponent_GetLayer(Collider) == CollisionLayer_Character);
            TEST_CHECK(run, WorldColliderComponent_IsPushable(Collider)
                && (WorldColliderComponent_GetMass(Collider) == CHARACTER_DEFAULT_MASS));
            TEST_CHECK(run, WorldColliderComponent_GetRotationMode(Collider) == WorldColliderRotationMode_YawOnly);
        }
        Vector3 Feet = GetFeet(&Scene, Id);
        TEST_CHECK(run, IsNear(Feet.x, 1.0f, 1e-6f) && IsNear(Feet.z, 2.0f, 1e-6f) && (Feet.y >= 0.0f)
            && (Feet.y < 0.003f));
        TEST_CHECK(run, GetState(&Scene, Id)->IsGrounded);
        TEST_CHECK(run, GetState(&Scene, Id)->EyeHeight == CHARACTER_DEFAULT_EYE_HEIGHT);
    }
    Scene_Deconstruct(run, &Scene);
}

/* A character standing on the floor keeps its feet the skin and the rest gap (2 mm) above it, to a micrometre, and
 * does not drift for 10 000 ticks: 173 km from the world's origin in the far run, where a float's step is 8 mm. (Its
 * ground follow lets the feet wobble about a tenth of a micrometre up and down, the same at the origin; across, they
 * stay put.) */
static void AStandingCharacterKeepsItsFeet2MmUpWithoutDrifting(TestRun* run)
{
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        // Across, the feet stay where they were put.
        Vector3Double Placed = TestWorld_At((Vector3){ 1.234f, 0.0f, -2.345f });
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 1.234f, 0.0f, -2.345f });
        TickFor(&Scene, TICKS_PER_SECOND);
        double Rest = (double)CHARACTER_SKIN + (double)CHARACTER_REST_GAP;
        double WorstHeight = 0.0;
        double Across = 0.0;
        bool IsAlwaysGrounded = true;
        for (uint32_t Index = 0U; Index <= 10000U; Index++)
        {
            Vector3Double Feet = WorldObject_GetWorldPosition(GetObject(&Scene, Id));
            WorstHeight = fmax(WorstHeight, fabs(Feet.Y - Placed.Y - Rest));
            Across = fmax(Across, hypot(Feet.X - Placed.X, Feet.Z - Placed.Z));
            IsAlwaysGrounded = IsAlwaysGrounded && GetState(&Scene, Id)->IsGrounded;
            Tick(&Scene);
        }
        TEST_CHECK(run, (WorstHeight < 1e-6) && (Across == 0.0) && IsAlwaysGrounded);
    }
    Scene_Deconstruct(run, &Scene);
}

/* The ticks of the square walk (WalkSquare): its five legs. */
#define SQUARE_WALK_TICKS (12U * TICKS_PER_SECOND)

/* A walk around the square, tick by tick: the character's feet and reported velocity, in the scene, and where the
 * crate it pushed ended. */
typedef struct SquareWalkStruct
{
    Vector3 Feet[SQUARE_WALK_TICKS];
    Vector3 Velocity[SQUARE_WALK_TICKS];
    Vector3 CrateEnd;
} SquareWalk;

/* Walks a character around a square on the floor, the scene shifted by @p offset: up a ramp turned 0.2 rad and off its
 * high end (a 0.6 m drop), across, back into a pushable crate it shoves along, and home; then it stops. The ramp's
 * slope and edge and the crate's pushes reach the parts of the motion a flat floor does not (the height above a slanted
 * ground, the speed walking off an edge, the collision step's pushes read back). */
static void WalkSquare(TestRun* run, Vector3Double offset, SquareWalk* outWalk)
{
    Vector3Double Previous = TestWorld_SetOffset(offset);
    Memory_Zero(outWalk, sizeof(*outWalk));
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        AddBox(run, &Scene, (Vector3){ 1.5f, -0.3f, 0.0f }, (Vector3){ 2.0f, 0.5f, 1.5f },
            (Vector3){ 0.0f, 0.0f, 0.2f }, TestBoxKind_Static);
        uint64_t Crate = AddBox(run, &Scene, (Vector3){ 2.0f, 0.3f, 3.04f }, (Vector3){ 0.3f, 0.3f, 0.3f },
            (Vector3){ 0 }, TestBoxKind_Pushable);
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ -1.513f, 0.0f, 0.237f });
        const Vector3 Legs[] = { { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { -1.0f, 0.0f, 0.0f },
            { 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f, 0.0f } };
        const uint32_t Seconds[] = { 4U, 2U, 3U, 2U, 1U };
        size_t Step = 0U;
        for (size_t Leg = 0U; Leg < (sizeof(Legs) / sizeof(Legs[0])); Leg++)
        {
            CharacterInput Input = CreateWalk(Legs[Leg]);
            SetInput(run, &Scene, Id, &Input);
            for (uint32_t Index = 0U; Index < (Seconds[Leg] * TICKS_PER_SECOND); Index++)
            {
                Tick(&Scene);
                outWalk->Feet[Step] = GetFeet(&Scene, Id);
                outWalk->Velocity[Step] = GetState(&Scene, Id)->Velocity;
                Step++;
            }
        }
        outWalk->CrateEnd = TestWorld_ToScene(WorldObject_GetWorldPosition(GetObject(&Scene, Crate)));
    }
    Scene_Deconstruct(run, &Scene);
    TestWorld_SetOffset(Previous);
}

/* The same walk around a square, at the world's origin and 173 km from it (where a float's step is 8 mm), goes
 * the same way tick by tick and ends at the same place of its scene, the crate it shoved too. */
static void ASquareWalkedFarAwayEndsWhereItEndsAtTheOrigin(TestRun* run)
{
    static SquareWalk AtOrigin;
    static SquareWalk FarAway;
    WalkSquare(run, Vector3Double_Zero(), &AtOrigin);
    WalkSquare(run, Vector3Double_Create(TEST_WORLD_FAR_OFFSET_X, TEST_WORLD_FAR_OFFSET_Y, TEST_WORLD_FAR_OFFSET_Z),
        &FarAway);
    float WorstFeet = 0.0f;
    float WorstVelocity = 0.0f;
    float Highest = 0.0f;
    for (size_t Index = 0U; Index < SQUARE_WALK_TICKS; Index++)
    {
        WorstFeet = fmaxf(WorstFeet, Vector3Distance(AtOrigin.Feet[Index], FarAway.Feet[Index]));
        WorstVelocity = fmaxf(WorstVelocity, Vector3Distance(AtOrigin.Velocity[Index], FarAway.Velocity[Index]));
        Highest = fmaxf(Highest, AtOrigin.Feet[Index].y);
    }
    // It went up the ramp, shoved the crate along and came home; far away the same, to a hundredth of a millimetre
    // (measured: within a micrometre). The reported velocity is a tick's move over the tick, so a micrometre between
    // the two walks reads as a few tenths of a millimetre a second; a float far out would read metres a second.
    TEST_CHECK(run, (Highest > 0.5f) && (AtOrigin.CrateEnd.x < 0.0f)
        && (Vector3Distance(AtOrigin.Feet[SQUARE_WALK_TICKS - 1U], (Vector3){ -1.513f, 0.0f, 0.237f }) < 2.0f));
    TEST_CHECK(run, (WorstFeet < 1e-5f) && (WorstVelocity < 1e-3f)
        && (Vector3Distance(AtOrigin.CrateEnd, FarAway.CrateEnd) < 1e-5f));
}

static void WalkingIntoAWallStopsJustShortOfIt(TestRun* run)
{
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        AddBox(run, &Scene, (Vector3){ 2.1f, 1.0f, 0.0f }, (Vector3){ 0.1f, 1.0f, 3.0f }, (Vector3){ 0 },
            TestBoxKind_Static);
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f });
        CharacterInput Input = CreateWalk((Vector3){ 1.0f, 0.0f, 0.0f });
        SetInput(run, &Scene, Id, &Input);
        TickFor(&Scene, 3U * TICKS_PER_SECOND);
        Vector3 Feet = GetFeet(&Scene, Id);
        // The wall's face is at x = 2: the capsule rests the skin and the rest gap from it, as above the floor.
        float Gap = CHARACTER_SKIN + CHARACTER_REST_GAP;
        TEST_CHECK(run, IsNear(Feet.x, 2.0f - TEST_RADIUS - Gap, 1e-4f) && IsNear(Feet.y, Gap, 1e-5f));
        TEST_CHECK(run, IsNear(Feet.z, 0.0f, 1e-4f));
        TEST_CHECK(run, fabsf(GetState(&Scene, Id)->Velocity.x) < 0.01f);
    }
    Scene_Deconstruct(run, &Scene);
}

static void WalkingDiagonallySlidesAlongAWall(TestRun* run)
{
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        AddBox(run, &Scene, (Vector3){ 1.1f, 1.0f, 0.0f }, (Vector3){ 0.1f, 1.0f, 10.0f }, (Vector3){ 0 },
            TestBoxKind_Static);
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f });
        CharacterInput Input = CreateWalk(Vector3Normalize((Vector3){ 1.0f, 0.0f, 1.0f }));
        SetInput(run, &Scene, Id, &Input);
        TickFor(&Scene, 2U * TICKS_PER_SECOND);
        Vector3 Feet = GetFeet(&Scene, Id);
        TEST_CHECK(run, (Feet.x < (1.0f - TEST_RADIUS)) && (Feet.x > (1.0f - TEST_RADIUS - 0.003f)));
        // It keeps the walk's part along the wall (about 1 m/s of the 1.4).
        TEST_CHECK(run, Feet.z > 1.5f);
    }
    Scene_Deconstruct(run, &Scene);
}

static void AnInsideCornerStopsWithoutJitter(TestRun* run)
{
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        AddBox(run, &Scene, (Vector3){ 1.1f, 1.0f, 0.0f }, (Vector3){ 0.1f, 1.0f, 3.0f }, (Vector3){ 0 },
            TestBoxKind_Static);
        AddBox(run, &Scene, (Vector3){ 0.0f, 1.0f, 1.1f }, (Vector3){ 3.0f, 1.0f, 0.1f }, (Vector3){ 0 },
            TestBoxKind_Static);
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f });
        CharacterInput Input = CreateWalk(Vector3Normalize((Vector3){ 1.0f, 0.0f, 1.0f }));
        Input.IsRunning = true;
        SetInput(run, &Scene, Id, &Input);
        TickFor(&Scene, 2U * TICKS_PER_SECOND);
        Vector3 Before = GetFeet(&Scene, Id);
        float Wander = 0.0f;
        for (uint32_t Index = 0U; Index < TICKS_PER_SECOND; Index++)
        {
            Tick(&Scene);
            Wander = Math_MaxFloat(Wander, Vector3Distance(GetFeet(&Scene, Id), Before));
        }
        TEST_CHECK(run, (Before.x < (1.0f - TEST_RADIUS)) && (Before.x > (1.0f - TEST_RADIUS - 0.003f)));
        TEST_CHECK(run, (Before.z < (1.0f - TEST_RADIUS)) && (Before.z > (1.0f - TEST_RADIUS - 0.003f)));
        TEST_CHECK(run, Wander < 1e-4f);
    }
    Scene_Deconstruct(run, &Scene);
}

/* Walks a character along +X from the origin toward a box step of @p height whose riser is at x = 1.5 (and which runs
 * on to x = 7.5); returns its feet after @p seconds. */
static Vector3 WalkAtAStep(TestRun* run, float height, uint32_t seconds, bool* outIsGrounded)
{
    Vector3 Feet = { 0 };
    *outIsGrounded = false;
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        AddBox(run, &Scene, (Vector3){ 4.5f, 0.5f * height, 0.0f }, (Vector3){ 3.0f, 0.5f * height, 2.0f },
            (Vector3){ 0 }, TestBoxKind_Static);
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f });
        CharacterInput Input = CreateWalk((Vector3){ 1.0f, 0.0f, 0.0f });
        SetInput(run, &Scene, Id, &Input);
        TickFor(&Scene, seconds * TICKS_PER_SECOND);
        Feet = GetFeet(&Scene, Id);
        *outIsGrounded = GetState(&Scene, Id)->IsGrounded;
    }
    Scene_Deconstruct(run, &Scene);
    return Feet;
}

static void ALowStepIsClimbedAndAHighOneIsNot(TestRun* run)
{
    bool IsGrounded = false;
    Vector3 Low = WalkAtAStep(run, 0.3f, 3U, &IsGrounded);
    TEST_CHECK(run, IsGrounded && (Low.x > 2.0f) && IsNear(Low.y, 0.3f, 0.003f));

    Vector3 High = WalkAtAStep(run, 0.5f, 3U, &IsGrounded);
    TEST_CHECK(run, IsGrounded && (High.x < (1.5f - TEST_RADIUS)) && (High.y < 0.003f));
}

/* A ramp of @p degrees rising along +X, @p length long, whose top surface starts at x = 1, @p lift above the floor. */
static void AddRaisedRamp(TestRun* run, CharacterScene* scene, float degrees, float lift, float length)
{
    float Angle = degrees * DEG2RAD;
    float HalfLength = 0.5f * length;
    float HalfThickness = 0.1f;
    // The top surface passes through (1, lift, 0): the box's centre is half its length up the slope from there, and
    // half its thickness below the surface.
    Vector3 Center = { 1.0f + (HalfLength * cosf(Angle)) + (HalfThickness * sinf(Angle)),
        lift + (HalfLength * sinf(Angle)) - (HalfThickness * cosf(Angle)), 0.0f };
    AddBox(run, scene, Center, (Vector3){ HalfLength, HalfThickness, 2.0f }, (Vector3){ 0.0f, 0.0f, Angle },
        TestBoxKind_Static);
}

/* A ramp of @p degrees rising along +X, 6 m long, whose top surface leaves the floor at x = 1. */
static void AddRamp(TestRun* run, CharacterScene* scene, float degrees)
{
    AddRaisedRamp(run, scene, degrees, 0.0f, 6.0f);
}

static void AGentleSlopeIsWalkedAndASteepOneIsNot(TestRun* run)
{
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        AddRamp(run, &Scene, 30.0f);
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f });
        CharacterInput Input = CreateWalk((Vector3){ 1.0f, 0.0f, 0.0f });
        SetInput(run, &Scene, Id, &Input);
        TickFor(&Scene, 2U * TICKS_PER_SECOND);
        Vector3 Feet = GetFeet(&Scene, Id);
        // On the ramp: the round bottom touches it uphill of the feet, which are r (1 / cos - 1) above its surface.
        float Angle = 30.0f * DEG2RAD;
        float Expected = ((Feet.x - 1.0f) * tanf(Angle)) + (TEST_RADIUS * ((1.0f / cosf(Angle)) - 1.0f));
        TEST_CHECK(run, GetState(&Scene, Id)->IsGrounded && (Feet.x > 2.5f) && IsNear(Feet.y, Expected, 0.005f));
    }
    Scene_Deconstruct(run, &Scene);

    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        AddRamp(run, &Scene, 60.0f);
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f });
        CharacterInput Input = CreateWalk((Vector3){ 1.0f, 0.0f, 0.0f });
        SetInput(run, &Scene, Id, &Input);
        TickFor(&Scene, 2U * TICKS_PER_SECOND);
        Vector3 Feet = GetFeet(&Scene, Id);
        TEST_CHECK(run, (Feet.x < 1.0f) && (Feet.y < 0.05f));

        // Dropped onto the steep slope, it slides down to the floor.
        TEST_CHECK_SUCCESS(run, WorldCharacters_Teleport(Scene.Characters, Id,
            TestWorld_At((Vector3){ 2.0f, 2.5f, 0.0f }), 0.0f));
        Input = CharacterInput_CreateDefault();
        SetInput(run, &Scene, Id, &Input);
        TickFor(&Scene, 3U * TICKS_PER_SECOND);
        Feet = GetFeet(&Scene, Id);
        TEST_CHECK(run, (Feet.x < 1.0f) && (Feet.y < 0.01f) && GetState(&Scene, Id)->IsGrounded);
    }
    Scene_Deconstruct(run, &Scene);
}

/* Walks a character down a flight of five 0.18 m steps (a landing 0.9 m high ending at x = 1, then steps 0.3 m deep)
 * for three seconds, with its step-down distance overridden to @p snap when @p isSnapOverridden: returns its feet, its
 * ground events and its ticks in the air. */
static Vector3 WalkDownStairs(TestRun* run, bool isSnapOverridden, float snap, EventCounts* outCounts,
    uint32_t* outAirborneTicks)
{
    Vector3 Feet = { 0 };
    *outAirborneTicks = 0U;
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        AddBox(run, &Scene, (Vector3){ -1.0f, 0.45f, 0.0f }, (Vector3){ 2.0f, 0.45f, 2.0f }, (Vector3){ 0 },
            TestBoxKind_Static);
        for (uint32_t Step = 1U; Step < 5U; Step++)
        {
            float Top = 0.9f - (0.18f * (float)Step);
            AddBox(run, &Scene, (Vector3){ 1.15f + (0.3f * (float)(Step - 1U)), 0.5f * Top, 0.0f },
                (Vector3){ 0.15f, 0.5f * Top, 2.0f }, (Vector3){ 0 }, TestBoxKind_Static);
        }
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.9f, 0.0f });
        if (isSnapOverridden)
        {
            OverrideFloat(run, &Scene, Id, CharacterSetting_GroundSnapDistance, snap);
        }
        TickFor(&Scene, 10U);
        EventCounts Placing = { 0 };
        TakeEvents(&Scene, Id, &Placing);
        CharacterInput Input = CreateWalk((Vector3){ 1.0f, 0.0f, 0.0f });
        SetInput(run, &Scene, Id, &Input);
        for (uint32_t Index = 0U; Index < (3U * TICKS_PER_SECOND); Index++)
        {
            Tick(&Scene);
            *outAirborneTicks += GetState(&Scene, Id)->IsGrounded ? 0U : 1U;
        }
        TakeEvents(&Scene, Id, outCounts);
        Feet = GetFeet(&Scene, Id);
    }
    Scene_Deconstruct(run, &Scene);
    return Feet;
}

/* By default a walker falls off every step, stairs too; a step-down distance keeps it on them. */
static void WalkingDownStairsFallsOffEachStepUnlessItStepsDown(TestRun* run)
{
    EventCounts Counts = { 0 };
    uint32_t AirborneTicks = 0U;
    Vector3 Feet = WalkDownStairs(run, false, 0.0f, &Counts, &AirborneTicks);
    TEST_CHECK(run, (Feet.x > 3.0f) && (Feet.y < 0.003f));
    TEST_CHECK(run, (Counts.Counts[WorldCharacterEventKind_LeftGround] == 5U)
        && (Counts.Counts[WorldCharacterEventKind_Landed] == 5U) && (AirborneTicks > 0U));

    EventCounts Stepping = { 0 };
    Feet = WalkDownStairs(run, true, 0.2f, &Stepping, &AirborneTicks);
    TEST_CHECK(run, (Feet.x > 3.0f) && (Feet.y < 0.003f));
    TEST_CHECK(run, (AirborneTicks == 0U) && (Stepping.Counts[WorldCharacterEventKind_LeftGround] == 0U));
}

static void SteppingOffALedgeFallsAndLands(TestRun* run)
{
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        AddBox(run, &Scene, (Vector3){ -1.0f, 0.5f, 0.0f }, (Vector3){ 2.0f, 0.5f, 2.0f }, (Vector3){ 0 },
            TestBoxKind_Static);
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 1.0f, 0.0f });
        TickFor(&Scene, 10U);
        EventCounts Counts = { 0 };
        TakeEvents(&Scene, Id, &Counts);
        CharacterInput Input = CreateWalk((Vector3){ 1.0f, 0.0f, 0.0f });
        SetInput(run, &Scene, Id, &Input);
        TickFor(&Scene, 2U * TICKS_PER_SECOND);
        TakeEvents(&Scene, Id, &Counts);
        Vector3 Feet = GetFeet(&Scene, Id);
        TEST_CHECK(run, (Feet.x > 1.5f) && (Feet.y < 0.003f) && GetState(&Scene, Id)->IsGrounded);
        TEST_CHECK(run, (Counts.Counts[WorldCharacterEventKind_LeftGround] == 1U)
            && (Counts.Counts[WorldCharacterEventKind_Landed] == 1U));
        // A metre's fall (the round bottom rolled off the edge first, its downward speed kept).
        float Fall = sqrtf(2.0f * WORLD_ENVIRONMENT_DEFAULT_GRAVITY);
        TEST_CHECK(run, (Counts.LastLandingSpeed > (0.95f * Fall)) && (Counts.LastLandingSpeed < (1.01f * Fall)));
    }
    Scene_Deconstruct(run, &Scene);
}


// Static functions: platforms and other characters.
static void AMovingPlatformCarriesACharacter(TestRun* run)
{
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        uint64_t Platform = AddBox(run, &Scene, (Vector3){ 0.0f, -0.1f, 0.0f }, (Vector3){ 2.0f, 0.1f, 2.0f },
            (Vector3){ 0 }, TestBoxKind_Kinematic);
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f });
        TickFor(&Scene, 10U);
        // The platform moves along +X at 1 m/s and up at 0.25 m/s, before the characters each tick.
        for (uint32_t Index = 0U; Index < TICKS_PER_SECOND; Index++)
        {
            WorldObject* Object = GetObject(&Scene, Platform);
            Vector3Double Position = WorldObject_GetPosition(Object);
            TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Object, Vector3Double_Add(Position,
                GameMath_FromVector3((Vector3){ TEST_DELTA, 0.25f * TEST_DELTA, 0.0f }))));
            Tick(&Scene);
        }
        Vector3 Feet = GetFeet(&Scene, Id);
        TEST_CHECK(run, IsNear(Feet.x, 1.0f, 0.01f) && IsNear(Feet.y, 0.25f, 0.005f));
        TEST_CHECK(run, GetState(&Scene, Id)->IsGrounded && (GetState(&Scene, Id)->GroundObjectId == Platform));
        TEST_CHECK(run, IsNear(GetState(&Scene, Id)->GroundVelocity.x, 1.0f, 0.01f));
    }
    Scene_Deconstruct(run, &Scene);
}

static void ATurningPlatformTurnsTheCharacter(TestRun* run)
{
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        uint64_t Platform = AddBox(run, &Scene, (Vector3){ 0.0f, -0.1f, 0.0f }, (Vector3){ 3.0f, 0.1f, 3.0f },
            (Vector3){ 0 }, TestBoxKind_Kinematic);
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 1.0f, 0.0f, 0.0f });
        TickFor(&Scene, 10U);
        float StartYaw = GetState(&Scene, Id)->FacingYaw;
        // A quarter turn a second about +Y, for a second.
        for (uint32_t Index = 0U; Index < TICKS_PER_SECOND; Index++)
        {
            WorldObject* Object = GetObject(&Scene, Platform);
            Vector3 Rotation = WorldObject_GetRotation(Object);
            Rotation.y += 0.5f * PI * TEST_DELTA;
            TEST_CHECK_SUCCESS(run, WorldObject_SetRotation(Object, Rotation));
            Tick(&Scene);
        }
        Vector3 Feet = GetFeet(&Scene, Id);
        // (1, 0, 0) turned a quarter turn about +Y is (0, 0, -1).
        TEST_CHECK(run, IsNear(Feet.x, 0.0f, 0.01f) && IsNear(Feet.z, -1.0f, 0.01f) && (Feet.y < 0.003f));
        float Turned = GetState(&Scene, Id)->FacingYaw - StartYaw;
        TEST_CHECK(run, IsNear(Turned, 0.5f * PI, 0.01f));
    }
    Scene_Deconstruct(run, &Scene);
}

static void TwoCharactersWalkingIntoEachOtherStop(TestRun* run)
{
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        uint64_t A = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f });
        uint64_t B = AddCharacter(run, &Scene, (Vector3){ 2.0f, 0.0f, 0.0f });
        CharacterInput East = CreateWalk((Vector3){ 1.0f, 0.0f, 0.0f });
        CharacterInput West = CreateWalk((Vector3){ -1.0f, 0.0f, 0.0f });
        SetInput(run, &Scene, A, &East);
        SetInput(run, &Scene, B, &West);
        float Closest = 10.0f;
        for (uint32_t Index = 0U; Index < (3U * TICKS_PER_SECOND); Index++)
        {
            Tick(&Scene);
            Closest = Math_MinFloat(Closest, Vector3Distance(GetFeet(&Scene, A), GetFeet(&Scene, B)));
        }
        Vector3 FeetA = GetFeet(&Scene, A);
        Vector3 FeetB = GetFeet(&Scene, B);
        TEST_CHECK(run, FeetA.x < FeetB.x);
        TEST_CHECK(run, Closest > ((2.0f * TEST_RADIUS) - 0.002f));
        // Equal masses: they meet in the middle.
        TEST_CHECK(run, IsNear(0.5f * (FeetA.x + FeetB.x), 1.0f, 0.01f));
    }
    Scene_Deconstruct(run, &Scene);
}

static void AHeavyCharacterPushesALightOne(TestRun* run)
{
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        uint64_t Heavy = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f });
        uint64_t Light = AddCharacter(run, &Scene, (Vector3){ 1.0f, 0.0f, 0.0f });
        OverrideFloat(run, &Scene, Heavy, CharacterSetting_Mass, 200.0f);
        OverrideFloat(run, &Scene, Light, CharacterSetting_Mass, 20.0f);
        CharacterInput East = CreateWalk((Vector3){ 1.0f, 0.0f, 0.0f });
        SetInput(run, &Scene, Heavy, &East);
        TickFor(&Scene, 2U * TICKS_PER_SECOND);
        Vector3 HeavyFeet = GetFeet(&Scene, Heavy);
        Vector3 LightFeet = GetFeet(&Scene, Light);
        // The heavy one walks on at most of its speed (a twentieth of the push is its own share), the light one is shoved
        // ahead of it.
        TEST_CHECK(run, LightFeet.x > 2.5f);
        TEST_CHECK(run, GetState(&Scene, Heavy)->Velocity.x > 1.2f);
        TEST_CHECK(run, (LightFeet.x - HeavyFeet.x) > ((2.0f * TEST_RADIUS) - 0.003f));
        TEST_CHECK(run, GetState(&Scene, Light)->IsGrounded);
    }
    Scene_Deconstruct(run, &Scene);
}


// Static functions: crouching, jumping, flying.
static void ACrouchedCharacterPassesUnderABeamAndStandsUpPastIt(TestRun* run)
{
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        // A beam whose bottom is 1.4 m up, from x = 1.8 to 2.2.
        AddBox(run, &Scene, (Vector3){ 2.0f, 1.6f, 0.0f }, (Vector3){ 0.2f, 0.2f, 3.0f }, (Vector3){ 0 },
            TestBoxKind_Static);
        uint64_t Standing = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.0f, 1.0f });
        uint64_t Crouching = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.0f, -1.0f });
        CharacterInput Walk = CreateWalk((Vector3){ 1.0f, 0.0f, 0.0f });
        SetInput(run, &Scene, Standing, &Walk);
        CharacterInput Crouch = Walk;
        Crouch.WantsCrouch = true;
        SetInput(run, &Scene, Crouching, &Crouch);
        TickFor(&Scene, TICKS_PER_SECOND);
        TEST_CHECK(run, GetState(&Scene, Crouching)->Stance == CharacterStance_Crouching);
        TEST_CHECK(run, IsNear(GetState(&Scene, Crouching)->Velocity.x, 1.0f, 1e-4f));
        WorldComponent* Component = NULL;
        if (World_TryGetComponent(&Scene.World, Crouching, WorldComponentType_Collider, &Component))
        {
            CollisionShape Shape = WorldColliderComponent_GetOwnShape(WorldColliderComponent_FromComponent(Component));
            TEST_CHECK(run, IsNear(Shape.Center.y, 0.5f * 1.15f, 1e-5f));
        }
        TEST_CHECK(run, GetState(&Scene, Crouching)->CrouchAmount == 1.0f);

        // Under the beam it lets go of crouching: it stays down until it is past it.
        TickFor(&Scene, TICKS_PER_SECOND);
        Vector3 Under = GetFeet(&Scene, Crouching);
        TEST_CHECK(run, (Under.x > (1.8f - TEST_RADIUS)) && (Under.x < (2.2f + TEST_RADIUS)));
        SetInput(run, &Scene, Crouching, &Walk);
        Tick(&Scene);
        TEST_CHECK(run, (GetState(&Scene, Crouching)->Stance == CharacterStance_Crouching)
            && GetState(&Scene, Crouching)->IsStandUpBlocked);
        TickFor(&Scene, 2U * TICKS_PER_SECOND);
        TEST_CHECK(run, GetState(&Scene, Crouching)->Stance == CharacterStance_Standing);
        TEST_CHECK(run, GetFeet(&Scene, Crouching).x > 2.5f);
        TEST_CHECK(run, GetState(&Scene, Crouching)->EyeHeight == CHARACTER_DEFAULT_EYE_HEIGHT);

        // Standing, the other was stopped by the beam.
        TEST_CHECK(run, GetFeet(&Scene, Standing).x < (1.8f - TEST_RADIUS));
    }
    Scene_Deconstruct(run, &Scene);
}

/* The highest the feet of a character reach after a jump press (with can_jump set as given). */
static float MeasureJump(TestRun* run, bool canJump, size_t* outJumps)
{
    float Highest = 0.0f;
    *outJumps = 0U;
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f });
        OverrideBool(run, &Scene, Id, CharacterSetting_CanJump, canJump);
        OverrideBool(run, &Scene, Id, CharacterSetting_CanAutoJump, false);
        TickFor(&Scene, 10U);
        float Start = GetFeet(&Scene, Id).y;
        CharacterInput Input = CharacterInput_CreateDefault();
        Input.WantsJump = true;
        SetInput(run, &Scene, Id, &Input);
        for (uint32_t Index = 0U; Index < TICKS_PER_SECOND; Index++)
        {
            Tick(&Scene);
            Highest = Math_MaxFloat(Highest, GetFeet(&Scene, Id).y - Start);
        }
        EventCounts Counts = { 0 };
        TakeEvents(&Scene, Id, &Counts);
        *outJumps = Counts.Counts[WorldCharacterEventKind_Jumped];
        // Held without auto-jumping, the request does not jump again: one jump, one landing.
        TEST_CHECK(run, Counts.Counts[WorldCharacterEventKind_Landed] == *outJumps);
    }
    Scene_Deconstruct(run, &Scene);
    return Highest;
}

static void AJumpReachesItsHeightOnlyWhenAllowed(TestRun* run)
{
    size_t Jumps = 0U;
    float Height = MeasureJump(run, true, &Jumps);
    // The air's drag takes a few millimetres.
    float Expected = (3.2f * 3.2f) / (2.0f * WORLD_ENVIRONMENT_DEFAULT_GRAVITY);
    TEST_CHECK(run, (Jumps == 1U) && IsNear(Height, Expected, 0.01f));
    Height = MeasureJump(run, false, &Jumps);
    TEST_CHECK(run, (Jumps == 0U) && (Height < 0.002f));
}

static void LateAndEarlyJumpsCount(TestRun* run)
{
    // Late: a jump pressed just after walking off an edge (within the coyote time) still jumps; later it does not.
    for (uint32_t Case = 0U; Case < 2U; Case++)
    {
        CharacterScene Scene;
        if (Scene_Construct(run, &Scene))
        {
            AddFloor(run, &Scene);
            AddBox(run, &Scene, (Vector3){ -1.0f, 1.0f, 0.0f }, (Vector3){ 2.0f, 1.0f, 2.0f }, (Vector3){ 0 },
                TestBoxKind_Static);
            uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 2.0f, 0.0f });
            OverrideBool(run, &Scene, Id, CharacterSetting_CanJump, true);
            CharacterInput Input = CreateWalk((Vector3){ 1.0f, 0.0f, 0.0f });
            SetInput(run, &Scene, Id, &Input);
            uint32_t Guard = 0U;
            while (GetState(&Scene, Id)->IsGrounded && (Guard < (2U * TICKS_PER_SECOND)))
            {
                Tick(&Scene);
                Guard++;
            }
            EventCounts Counts = { 0 };
            TakeEvents(&Scene, Id, &Counts);
            TickFor(&Scene, (Case == 0U) ? (TICKS_PER_SECOND / 40U) : (TICKS_PER_SECOND / 5U));
            Input.WantsJump = true;
            SetInput(run, &Scene, Id, &Input);
            Tick(&Scene);
            TakeEvents(&Scene, Id, &Counts);
            TEST_CHECK(run, Counts.Counts[WorldCharacterEventKind_Jumped] == ((Case == 0U) ? 1U : 0U));
        }
        Scene_Deconstruct(run, &Scene);
    }

    // Early: a jump pressed just before landing (within the buffer) jumps right after it.
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.5f, 0.0f });
        OverrideBool(run, &Scene, Id, CharacterSetting_CanJump, true);
        // Pressed 0.04 s before the 0.5 m fall ends (within the 0.1 s buffer).
        float FallSeconds = sqrtf((2.0f * 0.5f) / WORLD_ENVIRONMENT_DEFAULT_GRAVITY);
        TickFor(&Scene, (uint32_t)((FallSeconds - 0.04f) * (float)TICKS_PER_SECOND));
        EventCounts Counts = { 0 };
        TakeEvents(&Scene, Id, &Counts);
        TEST_CHECK(run, Counts.Counts[WorldCharacterEventKind_Landed] == 0U);
        CharacterInput Input = CharacterInput_CreateDefault();
        Input.WantsJump = true;
        SetInput(run, &Scene, Id, &Input);
        TickFor(&Scene, TICKS_PER_SECOND / 10U);
        TakeEvents(&Scene, Id, &Counts);
        TEST_CHECK(run, (Counts.Counts[WorldCharacterEventKind_Landed] == 1U)
            && (Counts.Counts[WorldCharacterEventKind_Jumped] == 1U));
    }
    Scene_Deconstruct(run, &Scene);
}

static void AFlyerClimbsSlidesUnderACeilingAndLandsWhenItStops(TestRun* run)
{
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        // A ceiling whose bottom is 4 m up.
        AddBox(run, &Scene, (Vector3){ 0.0f, 4.1f, 0.0f }, (Vector3){ 10.0f, 0.1f, 10.0f }, (Vector3){ 0 },
            TestBoxKind_Static);
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f });
        OverrideBool(run, &Scene, Id, CharacterSetting_CanFly, true);
        CharacterInput Input = CreateWalk((Vector3){ 0.0f, 1.0f, 0.0f });
        Input.WantsToFly = true;
        SetInput(run, &Scene, Id, &Input);
        TickFor(&Scene, 2U * TICKS_PER_SECOND);
        EventCounts Counts = { 0 };
        TakeEvents(&Scene, Id, &Counts);
        TEST_CHECK(run, Counts.Counts[WorldCharacterEventKind_TookOff] == 1U);
        TEST_CHECK(run, GetState(&Scene, Id)->Mode == CharacterMovementMode_Flying);
        Vector3 Feet = GetFeet(&Scene, Id);
        TEST_CHECK(run, (Feet.y < (4.0f - TEST_HEIGHT)) && (Feet.y > (4.0f - TEST_HEIGHT - 0.003f)));

        // Along +X and a little up: it slides along the ceiling.
        Input.MoveDirection = Vector3Normalize((Vector3){ 1.0f, 0.3f, 0.0f });
        SetInput(run, &Scene, Id, &Input);
        TickFor(&Scene, TICKS_PER_SECOND);
        Feet = GetFeet(&Scene, Id);
        TEST_CHECK(run, (Feet.x > 2.0f) && (Feet.y < (4.0f - TEST_HEIGHT)));

        // It stops flying: it falls and lands.
        Input = CharacterInput_CreateDefault();
        SetInput(run, &Scene, Id, &Input);
        TickFor(&Scene, 2U * TICKS_PER_SECOND);
        TakeEvents(&Scene, Id, &Counts);
        TEST_CHECK(run, (Counts.Counts[WorldCharacterEventKind_StoppedFlying] == 1U)
            && (Counts.Counts[WorldCharacterEventKind_Landed] == 1U));
        TEST_CHECK(run, GetState(&Scene, Id)->IsGrounded && (GetFeet(&Scene, Id).y < 0.003f));
        TEST_CHECK(run, GetState(&Scene, Id)->Mode == CharacterMovementMode_Walking);
    }
    Scene_Deconstruct(run, &Scene);
}

static void ACharacterThatMayNotFlyStaysOnItsFeet(TestRun* run)
{
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f });
        CharacterInput Input = CreateWalk((Vector3){ 0.0f, 1.0f, 0.0f });
        Input.WantsToFly = true;
        SetInput(run, &Scene, Id, &Input);
        TickFor(&Scene, TICKS_PER_SECOND);
        TEST_CHECK(run, (GetState(&Scene, Id)->Mode == CharacterMovementMode_Walking) && (GetFeet(&Scene, Id).y < 0.003f));
    }
    Scene_Deconstruct(run, &Scene);
}


// Static functions: robustness.
static void NothingTunnelsThroughThinWallsOrFloors(TestRun* run)
{
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        // A wall 2 cm thick, run into at 100 m/s.
        AddBox(run, &Scene, (Vector3){ 3.01f, 1.0f, 0.0f }, (Vector3){ 0.01f, 1.0f, 3.0f }, (Vector3){ 0 },
            TestBoxKind_Static);
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f });
        OverrideFloat(run, &Scene, Id, CharacterSetting_RunSpeed, 100.0f);
        OverrideFloat(run, &Scene, Id, CharacterSetting_Acceleration, 10000.0f);
        CharacterInput Input = CreateWalk((Vector3){ 1.0f, 0.0f, 0.0f });
        Input.IsRunning = true;
        SetInput(run, &Scene, Id, &Input);
        TickFor(&Scene, TICKS_PER_SECOND);
        TEST_CHECK(run, GetFeet(&Scene, Id).x < (3.0f - TEST_RADIUS));
    }
    Scene_Deconstruct(run, &Scene);

    if (Scene_Construct(run, &Scene))
    {
        // A floor 2 cm thick, fallen onto from 300 m at the fastest fall.
        AddBox(run, &Scene, (Vector3){ 0.0f, -0.01f, 0.0f }, (Vector3){ 5.0f, 0.01f, 5.0f }, (Vector3){ 0 },
            TestBoxKind_Static);
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 300.0f, 0.0f });
        TickFor(&Scene, 10U * TICKS_PER_SECOND);
        Vector3 Feet = GetFeet(&Scene, Id);
        TEST_CHECK(run, GetState(&Scene, Id)->IsGrounded && (Feet.y >= 0.0f) && (Feet.y < 0.003f));
    }
    Scene_Deconstruct(run, &Scene);
}

static void ACharacterNobodyDrivesFallsAndRides(TestRun* run)
{
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        uint64_t Platform = AddBox(run, &Scene, (Vector3){ 0.0f, -0.1f, 0.0f }, (Vector3){ 3.0f, 0.1f, 3.0f },
            (Vector3){ 0 }, TestBoxKind_Kinematic);
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 2.0f, 0.0f });
        TickFor(&Scene, TICKS_PER_SECOND);
        TEST_CHECK(run, GetState(&Scene, Id)->IsGrounded && (GetFeet(&Scene, Id).y < 0.003f));
        for (uint32_t Index = 0U; Index < TICKS_PER_SECOND; Index++)
        {
            WorldObject* Object = GetObject(&Scene, Platform);
            TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Object, Vector3Double_Add(WorldObject_GetPosition(Object),
                GameMath_FromVector3((Vector3){ 0.0f, 0.0f, 0.5f * TEST_DELTA }))));
            Tick(&Scene);
        }
        TEST_CHECK(run, IsNear(GetFeet(&Scene, Id).z, 0.5f, 0.01f));
    }
    Scene_Deconstruct(run, &Scene);
}

// Static functions: actions.
/* A control asking for an anchor at @p feet of the scene facing @p yaw, reached in @p seconds, with @p lock. */
static CharacterActionControl CreateAnchor(Vector3 feet, float yaw, float seconds, CharacterMovementLock lock)
{
    CharacterActionControl Control = CharacterActionControl_CreateFree();
    Control.Lock = lock;
    Control.HasAnchor = true;
    Control.AnchorPosition = TestWorld_At(feet);
    Control.AnchorYaw = yaw;
    Control.ApproachSeconds = seconds;
    return Control;
}

static void AnAnchorSlidesTheCharacterThereAndFacesItsWay(TestRun* run)
{
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f });
        TickFor(&Scene, 10U);
        // An anchor 1.2 m away facing the other way, in half a second: it slides there and turns, whatever its input
        // asks (it would walk off the other way).
        CharacterInput Away = CreateWalk((Vector3){ -1.0f, 0.0f, 0.0f });
        SetInput(run, &Scene, Id, &Away);
        CharacterActionControl Control = CreateAnchor((Vector3){ 0.0f, 0.0f, 1.2f }, PI, 0.5f,
            CharacterMovementLock_Locked);
        TEST_CHECK_SUCCESS(run, WorldCharacterComponent_SetActionControl(GetCharacter(&Scene, Id), &Control));
        TEST_CHECK(run, GetState(&Scene, Id)->Approach == CharacterApproach_Approaching);
        TickFor(&Scene, TICKS_PER_SECOND / 4U);
        Vector3 Halfway = GetFeet(&Scene, Id);
        TEST_CHECK(run, (GetState(&Scene, Id)->Approach == CharacterApproach_Approaching) && (Halfway.z > 0.4f)
            && (Halfway.z < 0.8f) && IsNear(Halfway.x, 0.0f, 1e-3f));
        TickFor(&Scene, TICKS_PER_SECOND / 3U);
        Vector3 Feet = GetFeet(&Scene, Id);
        const WorldCharacterState* State = GetState(&Scene, Id);
        TEST_CHECK(run, (State->Approach == CharacterApproach_Arrived) && IsNear(Feet.z, 1.2f, CHARACTER_ANCHOR_TOLERANCE)
            && IsNear(Feet.x, 0.0f, CHARACTER_ANCHOR_TOLERANCE) && State->IsGrounded
            && IsNear(fabsf(State->FacingYaw), PI, 1e-4f));
        // Held there, locked: its input does not move it, jumps do not happen, and its collider is not pushable.
        CharacterInput Jump = CreateWalk((Vector3){ 1.0f, 0.0f, 0.0f });
        Jump.WantsJump = true;
        SetInput(run, &Scene, Id, &Jump);
        TickFor(&Scene, TICKS_PER_SECOND / 2U);
        EventCounts Counts = { 0 };
        TakeEvents(&Scene, Id, &Counts);
        Vector3 Held = GetFeet(&Scene, Id);
        WorldComponent* Collider = NULL;
        TEST_CHECK(run, IsNear(Held.x, Feet.x, 1e-4f) && IsNear(Held.z, Feet.z, 1e-4f)
            && (Counts.Counts[WorldCharacterEventKind_Jumped] == 0U) && GetState(&Scene, Id)->IsGrounded);
        TEST_CHECK(run, World_TryGetComponent(&Scene.World, Id, WorldComponentType_Collider, &Collider)
            && !WorldColliderComponent_IsPushable(WorldColliderComponent_FromComponent(Collider)));
        // Freed: it walks again (and is pushable again).
        TEST_CHECK_SUCCESS(run, WorldCharacterComponent_SetActionControl(GetCharacter(&Scene, Id), NULL));
        TEST_CHECK(run, GetState(&Scene, Id)->Approach == CharacterApproach_None);
        SetInput(run, &Scene, Id, &Jump);
        TickFor(&Scene, TICKS_PER_SECOND / 2U);
        TEST_CHECK(run, (GetFeet(&Scene, Id).x > (Held.x + 0.3f))
            && WorldColliderComponent_IsPushable(WorldColliderComponent_FromComponent(Collider)));
    }
    Scene_Deconstruct(run, &Scene);
}

static void AWallOnTheWayFailsTheApproach(TestRun* run)
{
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        AddBox(run, &Scene, (Vector3){ 0.0f, 1.0f, 1.0f }, (Vector3){ 3.0f, 1.0f, 0.1f }, (Vector3){ 0 },
            TestBoxKind_Static);
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f });
        TickFor(&Scene, 10U);
        CharacterActionControl Control = CreateAnchor((Vector3){ 0.0f, 0.0f, 2.0f }, 0.0f, 0.4f,
            CharacterMovementLock_Locked);
        TEST_CHECK_SUCCESS(run, WorldCharacterComponent_SetActionControl(GetCharacter(&Scene, Id), &Control));
        // The slide stops at the wall (swept like any move), and once its time and the grace are past it fails.
        TickFor(&Scene, (uint32_t)(0.8f * (float)TICKS_PER_SECOND));
        TEST_CHECK(run, GetState(&Scene, Id)->Approach == CharacterApproach_Approaching);
        TickFor(&Scene, (uint32_t)(0.2f * (float)TICKS_PER_SECOND));
        Vector3 Feet = GetFeet(&Scene, Id);
        TEST_CHECK(run, (GetState(&Scene, Id)->Approach == CharacterApproach_Failed)
            && (Feet.z < (0.9f - TEST_RADIUS + 0.01f)));
        // An anchor control that is not finite, or a lock outside the enum, is refused and changes nothing.
        Control.AnchorYaw = NAN;
        TEST_CHECK_ERROR(run, WorldCharacterComponent_SetActionControl(GetCharacter(&Scene, Id), &Control),
            ErrorCode_ArgumentOutOfRange);
        Control.AnchorYaw = 0.0f;
        Control.ApproachSeconds = -1.0f;
        TEST_CHECK_ERROR(run, WorldCharacterComponent_SetActionControl(GetCharacter(&Scene, Id), &Control),
            ErrorCode_ArgumentOutOfRange);
        Control.ApproachSeconds = 0.0f;
        Control.Lock = (CharacterMovementLock)7;
        TEST_CHECK_ERROR(run, WorldCharacterComponent_SetActionControl(GetCharacter(&Scene, Id), &Control),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, GetState(&Scene, Id)->Approach == CharacterApproach_Failed);
        TEST_CHECK_ERROR(run, WorldCharacterComponent_SetActionControl(NULL, NULL), ErrorCode_IllegalArgument);
    }
    Scene_Deconstruct(run, &Scene);
}

static void LocksKeepTheCharacterFromMovingOrTurning(TestRun* run)
{
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f });
        TickFor(&Scene, 10U);
        // No moving: it does not walk or crouch, but faces its look.
        CharacterActionControl Control = CharacterActionControl_CreateFree();
        Control.Lock = CharacterMovementLock_NoMoving;
        TEST_CHECK_SUCCESS(run, WorldCharacterComponent_SetActionControl(GetCharacter(&Scene, Id), &Control));
        CharacterInput Input = CreateWalk((Vector3){ 1.0f, 0.0f, 0.0f });
        Input.WantsCrouch = true;
        Input.Facing = CharacterFacing_Look;
        Input.LookYaw = PI * 0.5f;
        SetInput(run, &Scene, Id, &Input);
        TickFor(&Scene, TICKS_PER_SECOND / 2U);
        TEST_CHECK(run, IsNear(GetFeet(&Scene, Id).x, 0.0f, 1e-4f)
            && (GetState(&Scene, Id)->Stance == CharacterStance_Standing)
            && IsNear(GetState(&Scene, Id)->FacingYaw, PI * 0.5f, 1e-3f) && (GetState(&Scene, Id)->Approach
            == CharacterApproach_None));
        // No turning: it walks but keeps its facing.
        Control.Lock = CharacterMovementLock_NoTurning;
        TEST_CHECK_SUCCESS(run, WorldCharacterComponent_SetActionControl(GetCharacter(&Scene, Id), &Control));
        Input = CreateWalk((Vector3){ 0.0f, 0.0f, 1.0f });
        Input.Facing = CharacterFacing_Movement;
        SetInput(run, &Scene, Id, &Input);
        TickFor(&Scene, TICKS_PER_SECOND / 2U);
        TEST_CHECK(run, (GetFeet(&Scene, Id).z > 0.3f) && IsNear(GetState(&Scene, Id)->FacingYaw, PI * 0.5f, 1e-3f));
    }
    Scene_Deconstruct(run, &Scene);
}

static void SettingsChangedAtRuntimeTakeEffect(TestRun* run)
{
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f });
        CharacterInput Input = CreateWalk((Vector3){ 0.0f, 0.0f, 1.0f });
        SetInput(run, &Scene, Id, &Input);
        TickFor(&Scene, TICKS_PER_SECOND);
        TEST_CHECK(run, IsNear(GetState(&Scene, Id)->Velocity.z, 1.4f, 1e-4f));
        OverrideFloat(run, &Scene, Id, CharacterSetting_WalkSpeed, 3.0f);
        TickFor(&Scene, TICKS_PER_SECOND);
        TEST_CHECK(run, IsNear(GetState(&Scene, Id)->Velocity.z, 3.0f, 1e-4f));
        // It faces where it walks.
        TEST_CHECK(run, IsNear(GetState(&Scene, Id)->FacingYaw, 0.0f, 1e-4f));
        // New eye heights (another kind of character) take effect at once; a crouch still eases.
        OverrideFloat(run, &Scene, Id, CharacterSetting_EyeHeight, 0.5f);
        OverrideFloat(run, &Scene, Id, CharacterSetting_CrouchEyeHeight, 0.3f);
        Tick(&Scene);
        TEST_CHECK(run, GetState(&Scene, Id)->EyeHeight == 0.5f);
        Input.MoveDirection = (Vector3){ 1.0f, 0.0f, 0.0f };
        SetInput(run, &Scene, Id, &Input);
        TickFor(&Scene, TICKS_PER_SECOND);
        TEST_CHECK(run, IsNear(GetState(&Scene, Id)->FacingYaw, 0.5f * PI, 1e-3f));
    }
    Scene_Deconstruct(run, &Scene);
}


// Static functions: corner cases of the motion (steps, landings, slides, lifts, curbs, re-enabling, teleports, walls).
/* Whether a character walking along +X at @p speed from x = @p startX stands on a step of @p height (its riser at
 * x = 1.5) after @p ticks. */
static bool ClimbsAStep(TestRun* run, float height, float speed, float startX, uint32_t ticks)
{
    bool IsOnTop = false;
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        AddBox(run, &Scene, (Vector3){ 4.5f, 0.5f * height, 0.0f }, (Vector3){ 3.0f, 0.5f * height, 2.0f },
            (Vector3){ 0 }, TestBoxKind_Static);
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ startX, 0.0f, 0.0f });
        OverrideFloat(run, &Scene, Id, CharacterSetting_WalkSpeed, speed);
        CharacterInput Input = CreateWalk((Vector3){ 1.0f, 0.0f, 0.0f });
        SetInput(run, &Scene, Id, &Input);
        TickFor(&Scene, ticks);
        IsOnTop = GetState(&Scene, Id)->IsGrounded && (GetFeet(&Scene, Id).y > (0.5f * height));
    }
    Scene_Deconstruct(run, &Scene);
    return IsOnTop;
}

/* A riser is climbed however little of the tick's move is left when the walker meets it, and by a slow walker too: a
 * step up going no further than the move left would stop short of the step's edge. */
static void ARiserIsClimbedWhereverTheTickMeetsIt(TestRun* run)
{
    static const float Heights[] = { 0.1f, 0.25f, 0.34f };
    static const float Speeds[] = { 0.3f, 1.4f, 3.5f };
    uint32_t Missed = 0U;
    for (size_t Height = 0U; Height < (sizeof(Heights) / sizeof(Heights[0])); Height++)
    {
        for (size_t Speed = 0U; Speed < (sizeof(Speeds) / sizeof(Speeds[0])); Speed++)
        {
            // Starts a sixth of a tick's move apart: the move meets the riser with more or less of it left.
            for (uint32_t Shift = 0U; Shift < 6U; Shift++)
            {
                float StartX = 0.9f - ((Speeds[Speed] * TEST_DELTA * (float)Shift) / 6.0f);
                Missed += ClimbsAStep(run, Heights[Height], Speeds[Speed], StartX, 2U * TICKS_PER_SECOND) ? 0U : 1U;
            }
        }
    }
    TEST_CHECK(run, Missed == 0U);
}

/* Drops a character from @p drop metres above the top of a pushable crate, or of another character's head: it must
 * land on it once and stand on it, the skin and the rest gap above it. */
static void DropOntoAPushable(TestRun* run, float drop, bool isOntoAHead)
{
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        uint64_t Below = isOntoAHead ? AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f })
            : AddBox(run, &Scene, (Vector3){ 0.0f, 0.5f, 0.0f }, (Vector3){ 0.5f, 0.5f, 0.5f }, (Vector3){ 0 },
                TestBoxKind_Pushable);
        TickFor(&Scene, 10U);
        float Top = isOntoAHead ? (GetFeet(&Scene, Below).y + TEST_HEIGHT) : 1.0f;
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, Top + drop, 0.0f });
        TickFor(&Scene, 2U * TICKS_PER_SECOND);
        EventCounts Counts = { 0 };
        TakeEvents(&Scene, Id, &Counts);
        TEST_CHECK(run, GetState(&Scene, Id)->IsGrounded && (GetState(&Scene, Id)->GroundObjectId == Below));
        TEST_CHECK(run, (Counts.Counts[WorldCharacterEventKind_Landed] == 1U)
            && (Counts.Counts[WorldCharacterEventKind_LeftGround] == 0U));
        Top = isOntoAHead ? (GetFeet(&Scene, Below).y + TEST_HEIGHT) : (GetFeet(&Scene, Below).y + 0.5f);
        TEST_CHECK(run, IsNear(GetFeet(&Scene, Id).y, Top + CHARACTER_SKIN + CHARACTER_REST_GAP, 1e-4f));
    }
    Scene_Deconstruct(run, &Scene);
}

/* A falling character lands on a crate or a head (pushables) and stands on it: were pushables left to the collision
 * step, it would sink in and be pushed back up tick after tick, landing late or never. (Drops beyond the step height:
 * nearer, the placement stands it on it.) */
static void ACharacterDroppedOntoACrateOrAHeadLandsOnIt(TestRun* run)
{
    static const float Drops[] = { 0.5f, 1.1f, 3.1f };
    for (size_t Drop = 0U; Drop < (sizeof(Drops) / sizeof(Drops[0])); Drop++)
    {
        DropOntoAPushable(run, Drops[Drop], false);
        DropOntoAPushable(run, Drops[Drop], true);
    }

    // A 2 cm pushable plank on a pillar, fallen onto from 30 m (5 cm a tick): it is found below, not passed straight
    // through in one tick.
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        AddBox(run, &Scene, (Vector3){ 0.0f, 0.5f, 0.0f }, (Vector3){ 0.1f, 0.5f, 0.1f }, (Vector3){ 0 },
            TestBoxKind_Static);
        uint64_t Plank = AddBox(run, &Scene, (Vector3){ 0.0f, 1.011f, 0.0f }, (Vector3){ 1.0f, 0.01f, 1.0f },
            (Vector3){ 0 }, TestBoxKind_Pushable);
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.6f, 31.0f, 0.0f });
        TickFor(&Scene, 3U * TICKS_PER_SECOND);
        TEST_CHECK(run, GetState(&Scene, Id)->IsGrounded && (GetState(&Scene, Id)->GroundObjectId == Plank)
            && (GetFeet(&Scene, Id).y > 1.0f));
    }
    Scene_Deconstruct(run, &Scene);
}

/* A number in [-1, 1) from a small generator (the same each run). */
static float NextSigned(uint32_t* state)
{
    *state = (*state * 1664525U) + 1013904223U;
    return ((float)(*state >> 8) / 8388608.0f) - 1.0f;
}

/* A move going into a plane at speed keeps its slide: the rounding left along the plane is weighed against the move's
 * length, not the short slide's own (which would turn about 1 in 500 at 10 m/s into no move at all). */
static void AFastMoveIntoAPlaneKeepsItsSlide(TestRun* run)
{
    static const float Speeds[] = { 10.0f, 55.0f, CHARACTER_MAX_SPEED };
    uint32_t State = 12345U;
    uint32_t Lost = 0U;
    for (size_t Speed = 0U; Speed < (sizeof(Speeds) / sizeof(Speeds[0])); Speed++)
    {
        for (uint32_t Case = 0U; Case < 10000U; Case++)
        {
            Vector3 Normal = Vector3Normalize((Vector3){ NextSigned(&State), NextSigned(&State), NextSigned(&State) });
            Vector3 Move = Vector3Scale(Vector3Normalize((Vector3){ NextSigned(&State), NextSigned(&State),
                NextSigned(&State) }), Speeds[Speed]);
            float Into = Vector3DotProduct(Move, Normal);
            if (Into > 0.0f)
            {
                Move = Vector3Negate(Move);
                Into = -Into;
            }
            Vector3 Slide = Vector3Subtract(Move, Vector3Scale(Normal, Into));
            Vector3 Clipped = CharacterMotion_ClipToPlanes(Move, &Normal, 1U);
            bool IsLost = (Vector3Length(Slide) > (1e-3f * Speeds[Speed])) && (Vector3Length(Clipped) == 0.0f);
            Lost += IsLost ? 1U : 0U;
        }
    }
    TEST_CHECK(run, Lost == 0U);
}

/* How fast a character standing on a lift moving vertically at @p liftSpeed rises in the air after jumping (between the
 * second and third tick after the jump: clear of the lift, whose collider the jump's own tick still sees where the
 * last collision step left it). */
static float JumpOffALift(TestRun* run, float liftSpeed)
{
    float Vertical = 0.0f;
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        uint64_t Lift = AddBox(run, &Scene, (Vector3){ 0.0f, 9.9f, 0.0f }, (Vector3){ 2.0f, 0.1f, 2.0f }, (Vector3){ 0 },
            TestBoxKind_Kinematic);
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 10.0f, 0.0f });
        OverrideBool(run, &Scene, Id, CharacterSetting_CanJump, true);
        OverrideFloat(run, &Scene, Id, CharacterSetting_JumpSpeed, 3.0f);
        CharacterInput Input = CharacterInput_CreateDefault();
        for (uint32_t Index = 0U; Index <= (TICKS_PER_SECOND / 4U); Index++)
        {
            // A quarter of a second's ride, then the jump.
            Input.WantsJump = Index == (TICKS_PER_SECOND / 4U);
            SetInput(run, &Scene, Id, &Input);
            WorldObject* Object = GetObject(&Scene, Lift);
            TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Object, Vector3Double_Add(WorldObject_GetPosition(Object),
                GameMath_FromVector3((Vector3){ 0.0f, liftSpeed * TEST_DELTA, 0.0f }))));
            Tick(&Scene);
        }
        Input.WantsJump = false;
        SetInput(run, &Scene, Id, &Input);
        Tick(&Scene);
        float Before = GetFeet(&Scene, Id).y;
        Tick(&Scene);
        Vertical = (GetFeet(&Scene, Id).y - Before) / TEST_DELTA;
    }
    Scene_Deconstruct(run, &Scene);
    return Vertical;
}

/* A jump off a moving lift adds the jump speed to the lift's vertical velocity (set to the jump speed alone, a jump off
 * a rising lift would fall back onto it). */
static void AJumpOffAMovingLiftKeepsTheLiftsVelocity(TestRun* run)
{
    // The jump speed (3 m/s) on top of the lift's, less two and a half ticks of gravity.
    float Fallen = 2.5f * WORLD_ENVIRONMENT_DEFAULT_GRAVITY * TEST_DELTA;
    TEST_CHECK(run, IsNear(JumpOffALift(run, 0.0f), 3.0f - Fallen, 0.02f));
    TEST_CHECK(run, IsNear(JumpOffALift(run, 2.0f), 5.0f - Fallen, 0.02f));
    TEST_CHECK(run, IsNear(JumpOffALift(run, -2.0f), 1.0f - Fallen, 0.02f));
}

/* A ground snap distance of 0, or a very short one, still keeps a character on flat ground: with no look for the ground
 * at all it would leave the ground every tick, and over the middle of a large floor box a short look must still find
 * the floor (a distance query reading the capsule millimetres inside it would find nothing). */
static void NoGroundSnapStillStandsOnFlatGround(TestRun* run)
{
    static const float Snaps[] = { 0.0f, 0.0005f, 0.002f };
    for (size_t Snap = 0U; Snap < (sizeof(Snaps) / sizeof(Snaps[0])); Snap++)
    {
        CharacterScene Scene;
        if (Scene_Construct(run, &Scene))
        {
            AddFloor(run, &Scene);
            uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f });
            OverrideFloat(run, &Scene, Id, CharacterSetting_GroundSnapDistance, Snaps[Snap]);
            uint32_t Airborne = 0U;
            for (uint32_t Index = 0U; Index < (2U * TICKS_PER_SECOND); Index++)
            {
                // Half a second standing, then walking.
                if (Index == (TICKS_PER_SECOND / 2U))
                {
                    CharacterInput Input = CreateWalk((Vector3){ 1.0f, 0.0f, 0.0f });
                    SetInput(run, &Scene, Id, &Input);
                }
                Tick(&Scene);
                Airborne += GetState(&Scene, Id)->IsGrounded ? 0U : 1U;
            }
            EventCounts Counts = { 0 };
            TakeEvents(&Scene, Id, &Counts);
            TEST_CHECK(run, (Airborne == 0U) && (Counts.Counts[WorldCharacterEventKind_LeftGround] == 0U)
                && (Counts.Counts[WorldCharacterEventKind_Landed] == 0U) && (GetFeet(&Scene, Id).x > 1.5f));
        }
        Scene_Deconstruct(run, &Scene);
    }
}

/* A collider hanging under a pushable (a handle on a crate) is no wall to the walker's sweep, which skips pushables and
 * what they carry: the walker shoves the crate instead of stopping at the handle. */
static void ACarriedPartPushesTheCrateItHangsFrom(TestRun* run)
{
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        uint64_t Crate = AddBox(run, &Scene, (Vector3){ 2.0f, 0.201f, 0.0f }, (Vector3){ 0.2f, 0.2f, 0.2f },
            (Vector3){ 0 }, TestBoxKind_Pushable);
        WorldComponent* Component = NULL;
        if (World_TryGetComponent(&Scene.World, Crate, WorldComponentType_Collider, &Component))
        {
            WorldColliderComponent* Collider = WorldColliderComponent_FromComponent(Component);
            TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetMass(Collider, 10.0f));
            TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetOverridden(Collider, WorldColliderSetting_Mass, true));
        }
        // A handle sticking out of the crate toward the walker, at its waist.
        uint64_t Handle = AddBox(run, &Scene, (Vector3){ 1.6f, 0.8f, 0.0f }, (Vector3){ 0.1f, 0.1f, 0.1f },
            (Vector3){ 0 }, TestBoxKind_Kinematic);
        TEST_CHECK_SUCCESS(run, World_SetParent(&Scene.World, Handle, Crate, true));
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f });
        CharacterInput Input = CreateWalk((Vector3){ 1.0f, 0.0f, 0.0f });
        SetInput(run, &Scene, Id, &Input);
        TickFor(&Scene, 3U * TICKS_PER_SECOND);
        TEST_CHECK(run, (GetFeet(&Scene, Crate).x > 3.5f) && (GetState(&Scene, Id)->Velocity.x > 1.0f));
    }
    Scene_Deconstruct(run, &Scene);
}

/* Walks a character with @p stepHeight and ground snap @p snap along +X into a curb @p height high (its riser at
 * x = 1.5) for three seconds, then one more: returns whether it ends on top (past the riser), with the ground events
 * of all four and how far it wandered in the last. */
static bool WalkIntoACurb(TestRun* run, float height, float stepHeight, float snap, size_t* outEvents,
    float* outWander)
{
    bool IsOnTop = false;
    *outEvents = 0U;
    *outWander = 0.0f;
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        AddBox(run, &Scene, (Vector3){ 4.5f, 0.5f * height, 0.0f }, (Vector3){ 3.0f, 0.5f * height, 2.0f },
            (Vector3){ 0 }, TestBoxKind_Static);
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f });
        OverrideFloat(run, &Scene, Id, CharacterSetting_StepHeight, stepHeight);
        OverrideFloat(run, &Scene, Id, CharacterSetting_GroundSnapDistance, snap);
        TickFor(&Scene, 10U);
        EventCounts Placing = { 0 };
        TakeEvents(&Scene, Id, &Placing);
        CharacterInput Input = CreateWalk((Vector3){ 1.0f, 0.0f, 0.0f });
        SetInput(run, &Scene, Id, &Input);
        TickFor(&Scene, 3U * TICKS_PER_SECOND);
        Vector3 Before = GetFeet(&Scene, Id);
        for (uint32_t Index = 0U; Index < TICKS_PER_SECOND; Index++)
        {
            Tick(&Scene);
            *outWander = Math_MaxFloat(*outWander, Vector3Distance(GetFeet(&Scene, Id), Before));
        }
        EventCounts Counts = { 0 };
        TakeEvents(&Scene, Id, &Counts);
        *outEvents = Counts.Counts[WorldCharacterEventKind_LeftGround] + Counts.Counts[WorldCharacterEventKind_Landed];
        IsOnTop = GetFeet(&Scene, Id).x > 1.5f;
    }
    Scene_Deconstruct(run, &Scene);
    return IsOnTop;
}

/* The round bottom rolls over any edge up to radius x (1 - cos slope_limit) high (8.8 cm for a human), so the step
 * height limits the edges it may roll over: with a step height of 0 a walker pressing against a curb stays put, neither
 * rolling onto it as up a slope nor, with a short ground snap, losing the ground and sliding up in the air. An edge
 * counts against the step height above the ground walked on. */
static void AStepHeightLimitsTheEdgesTheRoundBottomRollsOver(TestRun* run)
{
    static const float Curbs[] = { 0.002f, 0.005f, 0.02f, 0.05f, 0.08f };
    static const float Snaps[] = { 0.35f, 0.0f };
    for (size_t Curb = 0U; Curb < (sizeof(Curbs) / sizeof(Curbs[0])); Curb++)
    {
        for (size_t Snap = 0U; Snap < (sizeof(Snaps) / sizeof(Snaps[0])); Snap++)
        {
            // Pressing against it, it stays put: no ground events, no creeping or wobbling.
            size_t Events = 0U;
            float Wander = 0.0f;
            bool IsOnTop = WalkIntoACurb(run, Curbs[Curb], 0.0f, Snaps[Snap], &Events, &Wander);
            TEST_CHECK(run, !IsOnTop && (Events == 0U) && (Wander < 1e-4f));
        }
    }
    // A step height above the curb climbs it.
    size_t Events = 0U;
    float Wander = 0.0f;
    TEST_CHECK(run, WalkIntoACurb(run, 0.05f, 0.06f, 0.35f, &Events, &Wander));

    // A crest (a ramp's top onto level ground) is no step: crossed with a step height of 0 on the ground all along.
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        AddRamp(run, &Scene, 20.0f);
        float Crest = 6.0f * sinf(20.0f * DEG2RAD);
        float CrestX = 1.0f + (6.0f * cosf(20.0f * DEG2RAD));
        // The level top half a millimetre below the crest: no lip.
        AddBox(run, &Scene, (Vector3){ CrestX + 5.0f, 0.5f * (Crest - 0.0005f), 0.0f },
            (Vector3){ 5.0f, 0.5f * (Crest - 0.0005f), 2.0f }, (Vector3){ 0 }, TestBoxKind_Static);
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f });
        OverrideFloat(run, &Scene, Id, CharacterSetting_StepHeight, 0.0f);
        TickFor(&Scene, 10U);
        EventCounts Placing = { 0 };
        TakeEvents(&Scene, Id, &Placing);
        EventCounts Counts = { 0 };
        CharacterInput Input = CreateWalk((Vector3){ 1.0f, 0.0f, 0.0f });
        Input.IsRunning = true;
        SetInput(run, &Scene, Id, &Input);
        TickFor(&Scene, 3U * TICKS_PER_SECOND);
        TakeEvents(&Scene, Id, &Counts);
        Vector3 Feet = GetFeet(&Scene, Id);
        TEST_CHECK(run, (Feet.x > (CrestX + 1.0f)) && IsNear(Feet.y, Crest, 0.005f) && GetState(&Scene, Id)->IsGrounded);
        TEST_CHECK(run, (Counts.Counts[WorldCharacterEventKind_LeftGround] == 0U)
            && (Counts.Counts[WorldCharacterEventKind_Landed] == 0U));
    }
    Scene_Deconstruct(run, &Scene);

    // Nor is the way down off a curb: its edge is level with the ground left, and the ground is followed down.
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        AddBox(run, &Scene, (Vector3){ -2.0f, 0.025f, 0.0f }, (Vector3){ 3.0f, 0.025f, 2.0f }, (Vector3){ 0 },
            TestBoxKind_Static);
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.05f, 0.0f });
        OverrideFloat(run, &Scene, Id, CharacterSetting_StepHeight, 0.0f);
        TickFor(&Scene, 10U);
        EventCounts Placing = { 0 };
        TakeEvents(&Scene, Id, &Placing);
        EventCounts Counts = { 0 };
        CharacterInput Input = CreateWalk((Vector3){ 1.0f, 0.0f, 0.0f });
        SetInput(run, &Scene, Id, &Input);
        TickFor(&Scene, 2U * TICKS_PER_SECOND);
        TakeEvents(&Scene, Id, &Counts);
        Vector3 Feet = GetFeet(&Scene, Id);
        TEST_CHECK(run, (Feet.x > 2.0f) && (Feet.y < 0.003f) && GetState(&Scene, Id)->IsGrounded);
        TEST_CHECK(run, (Counts.Counts[WorldCharacterEventKind_LeftGround] == 0U)
            && (Counts.Counts[WorldCharacterEventKind_Landed] == 0U));
    }
    Scene_Deconstruct(run, &Scene);
}

/* Disabled on a platform that then moves away and enabled again with a jump, a character takes nothing of the
 * platform's move (as one tick's carry it would be 2 m in a tick, 960 m/s, kept as the jump's velocity). */
static void AReenabledCharacterIsNotThrownByWhatMovedMeanwhile(TestRun* run)
{
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        uint64_t Platform = AddBox(run, &Scene, (Vector3){ 0.0f, 0.1f, 0.0f }, (Vector3){ 1.5f, 0.1f, 1.5f },
            (Vector3){ 0 }, TestBoxKind_Kinematic);
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.2f, 0.0f });
        OverrideBool(run, &Scene, Id, CharacterSetting_CanJump, true);
        TickFor(&Scene, 10U);
        TEST_CHECK_SUCCESS(run, WorldObject_SetEnabled(GetObject(&Scene, Id), false));
        for (uint32_t Index = 0U; Index < TICKS_PER_SECOND; Index++)
        {
            WorldObject* Object = GetObject(&Scene, Platform);
            TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Object, Vector3Double_Add(WorldObject_GetPosition(Object),
                GameMath_FromVector3((Vector3){ 2.0f * TEST_DELTA, 0.0f, 0.0f }))));
            Tick(&Scene);
        }
        TEST_CHECK_SUCCESS(run, WorldObject_SetEnabled(GetObject(&Scene, Id), true));
        CharacterInput Input = CharacterInput_CreateDefault();
        Input.WantsJump = true;
        SetInput(run, &Scene, Id, &Input);
        Tick(&Scene);
        TEST_CHECK(run, (fabsf(GetState(&Scene, Id)->GroundVelocity.x) < 0.01f)
            && (fabsf(GetState(&Scene, Id)->Velocity.x) < 0.01f));
        TickFor(&Scene, 20U);
        TEST_CHECK(run, fabsf(GetFeet(&Scene, Id).x) < 0.01f);
    }
    Scene_Deconstruct(run, &Scene);
}

/* A teleport refuses a place the world cannot hold (not finite, or where the object's position, its local one
 * under a parent, would pass WORLD_POSITION_LIMIT, which the object's position row would refuse, leaving it where it
 * was without a word): the character stays put. */
static void ATeleportRefusesPlacesTheWorldCannotHold(TestRun* run)
{
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 1.0f, 0.0f, 2.0f });
        TickFor(&Scene, 10U);
        Vector3Double Before = WorldObject_GetWorldPosition(GetObject(&Scene, Id));
        const Vector3Double Refused[] =
        {
            { .X = NAN, .Y = 0.0, .Z = 0.0 }, { .X = 0.0, .Y = INFINITY, .Z = 0.0 },
            { .X = nextafter(WORLD_POSITION_LIMIT, INFINITY), .Y = 0.0, .Z = 0.0 },
            { .X = 0.0, .Y = 0.0, .Z = -nextafter(WORLD_POSITION_LIMIT, INFINITY) },
        };
        for (size_t Index = 0U; Index < (sizeof(Refused) / sizeof(Refused[0])); Index++)
        {
            TEST_CHECK_ERROR(run, WorldCharacters_Teleport(Scene.Characters, Id, Refused[Index], 0.0f),
                ErrorCode_ArgumentOutOfRange);
        }
        TEST_CHECK_ERROR(run, WorldCharacters_Teleport(Scene.Characters, Id, Before, NAN),
            ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, Vector3Double_Distance(WorldObject_GetWorldPosition(GetObject(&Scene, Id)), Before) == 0.0);
        // At the limit itself it goes (a root's world position is its own).
        Vector3Double Edge = Vector3Double_Create(WORLD_POSITION_LIMIT, 0.0, -WORLD_POSITION_LIMIT);
        TEST_CHECK_SUCCESS(run, WorldCharacters_Teleport(Scene.Characters, Id, Edge, 0.0f));
        TEST_CHECK(run, Vector3Double_Distance(WorldObject_GetWorldPosition(GetObject(&Scene, Id)), Edge) == 0.0);

        // Under a parent the row checks the local position: a world place within the limit whose local position is
        // not is refused, one nearer the parent goes; a parent flattened to nothing cannot take a place at all.
        TEST_CHECK_SUCCESS(run, WorldCharacters_Teleport(Scene.Characters, Id, Before, 0.0f));
        WorldObject* Holder = NULL;
        if (TEST_CHECK_SUCCESS(run, World_CreateObject(&Scene.World, (const unsigned char*)u8"holder", &Holder))
            && TEST_CHECK_SUCCESS(run, WorldObject_SetPosition(Holder, Vector3Double_Create(-9.0e6, 0.0, 0.0)))
            && TEST_CHECK_SUCCESS(run, World_SetParent(&Scene.World, Id, WorldObject_GetId(Holder), true)))
        {
            TEST_CHECK_ERROR(run, WorldCharacters_Teleport(Scene.Characters, Id, Vector3Double_Create(9.0e6, 0.0, 0.0),
                0.0f), ErrorCode_ArgumentOutOfRange);
            TEST_CHECK(run, Vector3Double_Distance(WorldObject_GetWorldPosition(GetObject(&Scene, Id)), Before) < 1e-6);
            Vector3Double Near = Vector3Double_Create(-8.5e6, 0.0, 0.0);
            TEST_CHECK_SUCCESS(run, WorldCharacters_Teleport(Scene.Characters, Id, Near, 0.0f));
            TEST_CHECK(run, Vector3Double_Distance(WorldObject_GetWorldPosition(GetObject(&Scene, Id)), Near) < 1e-6);
            TEST_CHECK_SUCCESS(run, WorldObject_SetScale(Holder, (Vector3){ 1.0f, 0.0f, 1.0f }));
            TEST_CHECK_ERROR(run, WorldCharacters_Teleport(Scene.Characters, Id, Near, 0.0f),
                ErrorCode_InvalidOperation);
        }
    }
    Scene_Deconstruct(run, &Scene);
}

/* A teleport places a character afresh: one teleported while in the air after a jump keeps no mark of that jump, so
 * walking off the next edge it may still jump late. */
static void ATeleportGivesBackTheLateJump(TestRun* run)
{
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        AddBox(run, &Scene, (Vector3){ -1.0f, 1.0f, 0.0f }, (Vector3){ 2.0f, 1.0f, 2.0f }, (Vector3){ 0 },
            TestBoxKind_Static);
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ -2.0f, 2.0f, 0.0f });
        OverrideBool(run, &Scene, Id, CharacterSetting_CanJump, true);
        TickFor(&Scene, 10U);
        // A jump, and while in the air a teleport onto the ledge (a script's "respawn here").
        CharacterInput Input = CharacterInput_CreateDefault();
        Input.WantsJump = true;
        SetInput(run, &Scene, Id, &Input);
        TickFor(&Scene, 20U);
        TEST_CHECK(run, !GetState(&Scene, Id)->IsGrounded);
        TEST_CHECK_SUCCESS(run, WorldCharacters_Teleport(Scene.Characters, Id,
            TestWorld_At((Vector3){ 0.0f, 2.0f, 0.0f }), 0.0f));
        Input = CreateWalk((Vector3){ 1.0f, 0.0f, 0.0f });
        SetInput(run, &Scene, Id, &Input);
        uint32_t Guard = 0U;
        while (GetState(&Scene, Id)->IsGrounded && (Guard < (2U * TICKS_PER_SECOND)))
        {
            Tick(&Scene);
            Guard++;
        }
        EventCounts Before = { 0 };
        TakeEvents(&Scene, Id, &Before);
        TickFor(&Scene, TICKS_PER_SECOND / 40U);
        Input.WantsJump = true;
        SetInput(run, &Scene, Id, &Input);
        Tick(&Scene);
        EventCounts Counts = { 0 };
        TakeEvents(&Scene, Id, &Counts);
        TEST_CHECK(run, (Before.Counts[WorldCharacterEventKind_LeftGround] == 1U)
            && (Counts.Counts[WorldCharacterEventKind_Jumped] == 1U));
    }
    Scene_Deconstruct(run, &Scene);
}

/* Adds a wall 80 m long, its face 1 m from the origin with its normal turned @p yawDegrees from -X; returns that normal. */
static Vector3 AddTurnedWall(TestRun* run, CharacterScene* scene, float yawDegrees)
{
    float Yaw = yawDegrees * DEG2RAD;
    Vector3 Normal = { -cosf(Yaw), 0.0f, sinf(Yaw) };
    Vector3 Center = Vector3Scale(Normal, -1.2f);
    AddBox(run, scene, (Vector3){ Center.x, 1.0f, Center.z }, (Vector3){ 0.2f, 1.0f, 40.0f },
        (Vector3){ 0.0f, Yaw, 0.0f }, TestBoxKind_Static);
    return Normal;
}

/* A run along a turned wall never stalls, and the collision step never pushes the walker: every move keeps the skin to
 * everything it passes and gives back the room rounding took. (Drifting into the wall a hair a tick, the capsule would
 * touch it and every move into it stop dead; and the collision step, which keeps the skin, would push walkers out of
 * walls and curbs every tick.) */
static void ARunAlongATurnedWallNeverStallsAndNothingPushesTheWalker(TestRun* run)
{
    static const float Speeds[] = { 3.5f, 30.0f };
    for (size_t Speed = 0U; Speed < (sizeof(Speeds) / sizeof(Speeds[0])); Speed++)
    {
        CharacterScene Scene;
        if (Scene_Construct(run, &Scene))
        {
            AddFloor(run, &Scene);
            Vector3 Normal = AddTurnedWall(run, &Scene, 17.0f);
            uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f });
            OverrideFloat(run, &Scene, Id, CharacterSetting_RunSpeed, Speeds[Speed]);
            OverrideFloat(run, &Scene, Id, CharacterSetting_Acceleration, 100.0f);
            Vector3 Along = { -Normal.z, 0.0f, Normal.x };
            if (Along.z < 0.0f)
            {
                Along = Vector3Negate(Along);
            }
            CharacterInput Input = CreateWalk(Vector3Normalize(Vector3Subtract(Along, Normal)));
            Input.IsRunning = true;
            SetInput(run, &Scene, Id, &Input);
            TickFor(&Scene, TICKS_PER_SECOND / 2U);
            float Slowest = FLT_MAX;
            float LargestPush = 0.0f;
            for (uint32_t Index = 0U; Index < TICKS_PER_SECOND; Index++)
            {
                WorldCharacters_Update(Scene.Characters, TEST_DELTA);
                Vector3 Moved = GetFeet(&Scene, Id);
                WorldCollision_Step(Scene.Collision, TEST_DELTA);
                World_UpdateTransforms(&Scene.World);
                LargestPush = Math_MaxFloat(LargestPush, Vector3Distance(GetFeet(&Scene, Id), Moved));
                Slowest = Math_MinFloat(Slowest, Vector3Length(GetState(&Scene, Id)->Velocity));
            }
            // Of the run, the part along the wall (cos 45 degrees) is kept.
            TEST_CHECK(run, Slowest > (0.6f * 0.70710678f * Speeds[Speed]));
            TEST_CHECK(run, LargestPush < 1e-6f);
        }
        Scene_Deconstruct(run, &Scene);
    }

    // Pressing into a curb's edge (a step height of 0): the step never moves it either.
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        AddBox(run, &Scene, (Vector3){ 4.5f, 0.025f, 0.0f }, (Vector3){ 3.0f, 0.025f, 2.0f }, (Vector3){ 0 },
            TestBoxKind_Static);
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f });
        OverrideFloat(run, &Scene, Id, CharacterSetting_StepHeight, 0.0f);
        CharacterInput Input = CreateWalk((Vector3){ 1.0f, 0.0f, 0.0f });
        SetInput(run, &Scene, Id, &Input);
        float LargestPush = 0.0f;
        for (uint32_t Index = 0U; Index < (2U * TICKS_PER_SECOND); Index++)
        {
            WorldCharacters_Update(Scene.Characters, TEST_DELTA);
            Vector3 Moved = GetFeet(&Scene, Id);
            WorldCollision_Step(Scene.Collision, TEST_DELTA);
            World_UpdateTransforms(&Scene.World);
            LargestPush = Math_MaxFloat(LargestPush, Vector3Distance(GetFeet(&Scene, Id), Moved));
        }
        TEST_CHECK(run, (LargestPush < 1e-6f) && (GetFeet(&Scene, Id).x > 1.3f));
    }
    Scene_Deconstruct(run, &Scene);
}


// Static functions: rolling off edges, jumps, air and ground control, the crouch's ease.
/* The horizontal speed of a character's last move. */
static float GetGroundSpeed(CharacterScene* scene, uint64_t id)
{
    Vector3 Velocity = GetState(scene, id)->Velocity;
    return sqrtf((Velocity.x * Velocity.x) + (Velocity.z * Velocity.z));
}

/* Walking off a block 0.3 m high, the walker rolls its round bottom down the edge only as far as the contact stays
 * walkable (r (1 - cos 45 degrees): 8.8 cm), and then falls, never staying on the ground to be snapped down the rest in
 * a tick. */
static void WalkingOffABlockRollsALittleThenFalls(TestRun* run)
{
    for (uint32_t Running = 0U; Running < 2U; Running++)
    {
        CharacterScene Scene;
        if (Scene_Construct(run, &Scene))
        {
            AddFloor(run, &Scene);
            AddBox(run, &Scene, (Vector3){ -1.0f, 0.15f, 0.0f }, (Vector3){ 2.0f, 0.15f, 2.0f }, (Vector3){ 0 },
                TestBoxKind_Static);
            uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.3f, 0.0f });
            TickFor(&Scene, 10U);
            EventCounts Placing = { 0 };
            TakeEvents(&Scene, Id, &Placing);
            CharacterInput Input = CreateWalk((Vector3){ 1.0f, 0.0f, 0.0f });
            Input.IsRunning = Running == 1U;
            SetInput(run, &Scene, Id, &Input);
            float LargestDrop = 0.0f;
            float LowestOnTheEdge = FLT_MAX;
            Vector3 Last = GetFeet(&Scene, Id);
            for (uint32_t Index = 0U; Index < (2U * TICKS_PER_SECOND); Index++)
            {
                Tick(&Scene);
                Vector3 Feet = GetFeet(&Scene, Id);
                LargestDrop = Math_MaxFloat(LargestDrop, Last.y - Feet.y);
                if (GetState(&Scene, Id)->IsGrounded && (Feet.x > 1.0f) && (Feet.y > 0.1f))
                {
                    LowestOnTheEdge = Math_MinFloat(LowestOnTheEdge, Feet.y);
                }
                Last = Feet;
            }
            EventCounts Counts = { 0 };
            TakeEvents(&Scene, Id, &Counts);
            float Roll = TEST_RADIUS * (1.0f - cosf(0.25f * PI));
            TEST_CHECK(run, LowestOnTheEdge >= (0.3f - Roll - 0.003f));
            // No drop of more than a centimetre in a tick: the roll and the fall, never a snap.
            TEST_CHECK(run, LargestDrop < 0.01f);
            TEST_CHECK(run, (Counts.Counts[WorldCharacterEventKind_LeftGround] == 1U)
                && (Counts.Counts[WorldCharacterEventKind_Landed] == 1U) && (Last.y < 0.003f));
        }
        Scene_Deconstruct(run, &Scene);
    }
}

/* Running off a ledge higher than the step height, the walker does not keep the downward speed of its roll over the
 * edge, as fast as it ran (it would dive): a fall from the top of the edge sets the most it keeps, so a metre's drop
 * lands at a metre's fall. */
static void RunningOffALedgeLandsAsFromItsTop(TestRun* run)
{
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        AddBox(run, &Scene, (Vector3){ -1.0f, 0.5f, 0.0f }, (Vector3){ 2.0f, 0.5f, 2.0f }, (Vector3){ 0 },
            TestBoxKind_Static);
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ -2.5f, 1.0f, 0.0f });
        TickFor(&Scene, 10U);
        EventCounts Counts = { 0 };
        TakeEvents(&Scene, Id, &Counts);
        CharacterInput Input = CreateWalk((Vector3){ 1.0f, 0.0f, 0.0f });
        Input.IsRunning = true;
        SetInput(run, &Scene, Id, &Input);
        TickFor(&Scene, 2U * TICKS_PER_SECOND);
        TakeEvents(&Scene, Id, &Counts);
        float Fall = sqrtf(2.0f * WORLD_ENVIRONMENT_DEFAULT_GRAVITY);
        TEST_CHECK(run, (Counts.Counts[WorldCharacterEventKind_Landed] == 1U)
            && (Counts.LastLandingSpeed > (0.97f * Fall)) && (Counts.LastLandingSpeed < (1.01f * Fall)));
    }
    Scene_Deconstruct(run, &Scene);
}

/* Running down a ramp that ends in the air, the walker keeps falling at about the ramp's rate (it does not stop to
 * fall from rest, nor dive at the rate of its roll over the ramp's end). */
static void RunningOffARampsEndKeepsItsDescent(TestRun* run)
{
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        AddRaisedRamp(run, &Scene, 30.0f, 2.0f, 6.0f);
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 4.0f, 2.0f + (3.0f * tanf(30.0f * DEG2RAD)) + 0.05f,
            0.0f });
        TickFor(&Scene, 10U);
        TEST_CHECK(run, GetState(&Scene, Id)->IsGrounded);
        CharacterInput Input = CreateWalk((Vector3){ -1.0f, 0.0f, 0.0f });
        Input.IsRunning = true;
        SetInput(run, &Scene, Id, &Input);
        for (uint32_t Index = 0U; (Index < (3U * TICKS_PER_SECOND)) && GetState(&Scene, Id)->IsGrounded; Index++)
        {
            Tick(&Scene);
        }
        // Its first move in the air (the tick it left still rolled over the ramp's end). The ramp's descent at a run is
        // 3.5 tan 30 degrees, about 2 m/s.
        Tick(&Scene);
        float Descent = -GetState(&Scene, Id)->Velocity.y;
        float Ramp = CharacterSettings_CreateDefault().RunSpeed * tanf(30.0f * DEG2RAD);
        TEST_CHECK(run, !GetState(&Scene, Id)->IsGrounded && (Descent > (0.9f * Ramp)) && (Descent < (1.4f * Ramp)));
    }
    Scene_Deconstruct(run, &Scene);
}

/* Holding jump, a character hops again as soon as it is back on the ground, but never sooner than the auto-jump delay
 * after its last jump. */
static void AHeldJumpJumpsAgainOnceTheDelayHasPassed(TestRun* run)
{
    static const float Delays[] = { 0.25f, 1.0f };
    float Hop = (2.0f * CharacterSettings_CreateDefault().JumpSpeed) / WORLD_ENVIRONMENT_DEFAULT_GRAVITY;
    for (size_t Delay = 0U; Delay < (sizeof(Delays) / sizeof(Delays[0])); Delay++)
    {
        CharacterScene Scene;
        if (Scene_Construct(run, &Scene))
        {
            AddFloor(run, &Scene);
            uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f });
            OverrideBool(run, &Scene, Id, CharacterSetting_CanJump, true);
            OverrideFloat(run, &Scene, Id, CharacterSetting_AutoJumpDelaySeconds, Delays[Delay]);
            TickFor(&Scene, 10U);
            WorldCharacters_ClearEvents(Scene.Characters);
            CharacterInput Input = CharacterInput_CreateDefault();
            Input.WantsJump = true;
            SetInput(run, &Scene, Id, &Input);
            uint64_t JumpTicks[16];
            size_t JumpCount = 0U;
            for (uint32_t Index = 0U; Index < (3U * TICKS_PER_SECOND); Index++)
            {
                Tick(&Scene);
                size_t Count = 0U;
                const WorldCharacterEvent* Events = WorldCharacters_GetEvents(Scene.Characters, &Count);
                for (size_t Event = 0U; Event < Count; Event++)
                {
                    if ((Events[Event].Kind == WorldCharacterEventKind_Jumped) && (JumpCount < 16U))
                    {
                        JumpTicks[JumpCount++] = Events[Event].Tick;
                    }
                }
                WorldCharacters_ClearEvents(Scene.Characters);
            }
            // A hop lands, and the next jump waits for the delay when the hop was shorter (a tick or two of rounding).
            float Expected = Math_MaxFloat(Hop, Delays[Delay]) * (float)TICKS_PER_SECOND;
            TEST_CHECK(run, JumpCount >= 3U);
            for (size_t Jump = 1U; Jump < JumpCount; Jump++)
            {
                TEST_CHECK(run, IsNear((float)(JumpTicks[Jump] - JumpTicks[Jump - 1U]), Expected, 3.0f));
            }
        }
        Scene_Deconstruct(run, &Scene);
    }
}

/* A sprint-jump adds the running jump boost along the keys, up to the running speed plus the boost: jumps in a row do
 * not add up. Without a boost (the default) a jump keeps the speed it had. */
static void ASprintJumpAddsItsBoostWithoutAddingUp(TestRun* run)
{
    for (uint32_t Boosted = 0U; Boosted < 2U; Boosted++)
    {
        CharacterScene Scene;
        if (Scene_Construct(run, &Scene))
        {
            AddFloor(run, &Scene);
            uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f });
            OverrideBool(run, &Scene, Id, CharacterSetting_CanJump, true);
            if (Boosted == 1U)
            {
                OverrideFloat(run, &Scene, Id, CharacterSetting_RunJumpBoost, 1.0f);
            }
            CharacterInput Input = CreateWalk((Vector3){ 1.0f, 0.0f, 0.0f });
            Input.IsRunning = true;
            SetInput(run, &Scene, Id, &Input);
            TickFor(&Scene, TICKS_PER_SECOND);
            Input.WantsJump = true;
            SetInput(run, &Scene, Id, &Input);
            float Fastest = 0.0f;
            for (uint32_t Index = 0U; Index < (4U * TICKS_PER_SECOND); Index++)
            {
                Tick(&Scene);
                Fastest = Math_MaxFloat(Fastest, GetGroundSpeed(&Scene, Id));
            }
            float Run = CharacterSettings_CreateDefault().RunSpeed;
            TEST_CHECK(run, IsNear(Fastest, Run + ((Boosted == 1U) ? 1.0f : 0.0f), 0.02f));
        }
        Scene_Deconstruct(run, &Scene);
    }
}

/* A sprint-jump up a slope, the keys pushing on uphill, lands where its flight off the slope ends: coming down on the
 * slope, the move slides it up along it, rising, which must not keep it in the air (it would slide on up the slope,
 * unable to jump, for as long as the keys push). Once down, it runs on on the ground and jumps again. */
static void ASprintJumpUpASlopeLandsOnIt(TestRun* run)
{
    static const float Slopes[] = { 10.0f, 20.0f, 30.0f };
    float Run = 5.0f;
    float Jump = 5.0f;
    float Boost = 1.0f;
    for (size_t Slope = 0U; Slope < (sizeof(Slopes) / sizeof(Slopes[0])); Slope++)
    {
        CharacterScene Scene;
        if (Scene_Construct(run, &Scene))
        {
            AddFloor(run, &Scene);
            AddRaisedRamp(run, &Scene, Slopes[Slope], 0.0f, 20.0f);
            uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f });
            OverrideBool(run, &Scene, Id, CharacterSetting_CanJump, true);
            OverrideBool(run, &Scene, Id, CharacterSetting_CanAutoJump, false);
            OverrideFloat(run, &Scene, Id, CharacterSetting_RunSpeed, Run);
            OverrideFloat(run, &Scene, Id, CharacterSetting_JumpSpeed, Jump);
            OverrideFloat(run, &Scene, Id, CharacterSetting_RunJumpBoost, Boost);
            OverrideFloat(run, &Scene, Id, CharacterSetting_AirAcceleration, 4.0f);
            CharacterInput Input = CreateWalk((Vector3){ 1.0f, 0.0f, 0.0f });
            Input.IsRunning = true;
            SetInput(run, &Scene, Id, &Input);
            TickFor(&Scene, (3U * TICKS_PER_SECOND) / 2U);
            TEST_CHECK(run, GetState(&Scene, Id)->IsGrounded && (GetFeet(&Scene, Id).x > 3.0f));
            WorldCharacters_ClearEvents(Scene.Characters);

            Input.WantsJump = true;
            SetInput(run, &Scene, Id, &Input);
            Tick(&Scene);
            Input.WantsJump = false;
            SetInput(run, &Scene, Id, &Input);
            uint32_t Airborne = 0U;
            while (!GetState(&Scene, Id)->IsGrounded && (Airborne < (3U * TICKS_PER_SECOND)))
            {
                Tick(&Scene);
                Airborne++;
            }
            EventCounts Counts = { 0 };
            TakeEvents(&Scene, Id, &Counts);
            // The flight off the slope: twice the jump's speed away from it over gravity's pull into it (the air's
            // drag changes it by less than a tick).
            float Angle = Slopes[Slope] * DEG2RAD;
            float Away = (Jump * cosf(Angle)) - ((Run + Boost) * sinf(Angle));
            float Flight = (2.0f * Away * (float)TICKS_PER_SECOND) / (WORLD_ENVIRONMENT_DEFAULT_GRAVITY * cosf(Angle));
            TEST_CHECK(run, (Counts.Counts[WorldCharacterEventKind_Jumped] == 1U)
                && (Counts.Counts[WorldCharacterEventKind_Landed] == 1U)
                && IsNear((float)Airborne, Flight, (0.02f * Flight) + 2.0f));

            TickFor(&Scene, TICKS_PER_SECOND / 4U);
            TEST_CHECK(run, GetState(&Scene, Id)->IsGrounded);
            Input.WantsJump = true;
            SetInput(run, &Scene, Id, &Input);
            Tick(&Scene);
            TakeEvents(&Scene, Id, &Counts);
            TEST_CHECK(run, Counts.Counts[WorldCharacterEventKind_Jumped] == 2U);
        }
        Scene_Deconstruct(run, &Scene);
    }
}

/* Jumps a character running at full speed along +X, then holds @p keys (nothing, along -X or along +Z) in the air;
 * returns its velocity across the ground after @p seconds in the air, and the fastest it went there. */
static Vector3 SteerInTheAir(TestRun* run, float airAcceleration, Vector3 keys, bool isRunning, float seconds,
    float* outFastest)
{
    Vector3 Velocity = { 0 };
    *outFastest = 0.0f;
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f });
        OverrideBool(run, &Scene, Id, CharacterSetting_CanJump, true);
        OverrideFloat(run, &Scene, Id, CharacterSetting_JumpSpeed, 6.0f);
        OverrideFloat(run, &Scene, Id, CharacterSetting_AirAcceleration, airAcceleration);
        CharacterInput Input = CreateWalk((Vector3){ isRunning ? 1.0f : 0.0f, 0.0f, 0.0f });
        Input.IsRunning = isRunning;
        SetInput(run, &Scene, Id, &Input);
        TickFor(&Scene, TICKS_PER_SECOND);
        Input.WantsJump = true;
        SetInput(run, &Scene, Id, &Input);
        Tick(&Scene);
        Input = CreateWalk(keys);
        Input.IsRunning = isRunning;
        SetInput(run, &Scene, Id, &Input);
        for (uint32_t Index = 0U; Index < (uint32_t)(seconds * (float)TICKS_PER_SECOND); Index++)
        {
            Tick(&Scene);
            *outFastest = Math_MaxFloat(*outFastest, GetGroundSpeed(&Scene, Id));
        }
        TEST_CHECK(run, !GetState(&Scene, Id)->IsGrounded);
        Velocity = GetState(&Scene, Id)->Velocity;
        Velocity.y = 0.0f;
    }
    Scene_Deconstruct(run, &Scene);
    return Velocity;
}

/* In the air the keys do nothing by default (letting go does not brake the character toward a standstill as on the
 * ground); with air control they push along their direction up to the wished speed, steering and braking but never
 * raising the speed across the ground past it (or past what it had). */
static void TheAirKeysSteerAndBrakeButNeverSpeedUp(TestRun* run)
{
    float Run = CharacterSettings_CreateDefault().RunSpeed;
    float Walk = CharacterSettings_CreateDefault().WalkSpeed;
    float Fastest = 0.0f;
    // Let go in the air, without air control: the run carries on (the air's drag, on a jump's 7 m/s, takes 5 cm/s).
    Vector3 Velocity = SteerInTheAir(run, 0.0f, (Vector3){ 0 }, true, 0.5f, &Fastest);
    TEST_CHECK(run, (Velocity.x > (Run - 0.1f)) && (Velocity.x <= Run));
    // From a standing jump, forward: it speeds up at the air acceleration, to the walking speed and no further.
    Velocity = SteerInTheAir(run, 4.0f, (Vector3){ 1.0f, 0.0f, 0.0f }, false, 0.25f, &Fastest);
    TEST_CHECK(run, IsNear(Velocity.x, 1.0f, 0.02f));
    Velocity = SteerInTheAir(run, 4.0f, (Vector3){ 1.0f, 0.0f, 0.0f }, false, 0.7f, &Fastest);
    TEST_CHECK(run, IsNear(Velocity.x, Walk, 0.01f) && (Fastest <= (Walk + 1e-3f)));
    // Sideways at a run: it turns toward the keys, no faster.
    Velocity = SteerInTheAir(run, 4.0f, (Vector3){ 0.0f, 0.0f, 1.0f }, true, 0.5f, &Fastest);
    TEST_CHECK(run, (Velocity.z > 1.0f) && (Velocity.x < (Run - 0.1f)) && (Fastest <= (Run + 1e-3f)));
    // Back: it brakes at the air acceleration.
    Velocity = SteerInTheAir(run, 4.0f, (Vector3){ -1.0f, 0.0f, 0.0f }, true, 0.25f, &Fastest);
    TEST_CHECK(run, IsNear(Velocity.x, Run - 1.0f, 0.03f));
}

/* A long fall slows to the speed where the air's drag (the drag times the speed squared) matches gravity. */
static void AFallReachesTheAirsTerminalSpeed(TestRun* run)
{
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 500.0f, 0.0f });
        OverrideFloat(run, &Scene, Id, CharacterSetting_AirDrag, 0.05f);
        TickFor(&Scene, 6U * TICKS_PER_SECOND);
        float Terminal = sqrtf(WORLD_ENVIRONMENT_DEFAULT_GRAVITY / 0.05f);
        TEST_CHECK(run, !GetState(&Scene, Id)->IsGrounded
            && IsNear(-GetState(&Scene, Id)->Velocity.y, Terminal, 0.01f * Terminal));
    }
    Scene_Deconstruct(run, &Scene);
}

/* Walks a character with ground control @p control (acceleration 64, friction 16) diagonally into a wall along Z for
 * two seconds; returns its speed along the wall. */
static float SlideAlongAWall(TestRun* run, CharacterGroundControl control)
{
    float Speed = 0.0f;
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        AddBox(run, &Scene, (Vector3){ 1.1f, 1.0f, 0.0f }, (Vector3){ 0.1f, 1.0f, 10.0f }, (Vector3){ 0 },
            TestBoxKind_Static);
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f });
        Override(run, &Scene, Id, CharacterSetting_GroundControl, PropertyValue_FromEnum((int32_t)control));
        OverrideFloat(run, &Scene, Id, CharacterSetting_Acceleration, 64.0f);
        OverrideFloat(run, &Scene, Id, CharacterSetting_GroundFriction, 16.0f);
        CharacterInput Input = CreateWalk(Vector3Normalize((Vector3){ 1.0f, 0.0f, 1.0f }));
        SetInput(run, &Scene, Id, &Input);
        TickFor(&Scene, 2U * TICKS_PER_SECOND);
        Speed = GetState(&Scene, Id)->Velocity.z;
    }
    Scene_Deconstruct(run, &Scene);
    return Speed;
}

/* Pressed diagonally into a wall, a walker slides along it: steering at the walk's share along the wall (cos 45
 * degrees), pushing at the walking speed, never past it (without the cap, the walking speed over cos 45). */
static void BothGroundControlsSlideAlongAWallNoFasterThanTheyWalk(TestRun* run)
{
    float Walk = CharacterSettings_CreateDefault().WalkSpeed;
    TEST_CHECK(run, IsNear(SlideAlongAWall(run, CharacterGroundControl_Steer), 0.70710678f * Walk, 0.01f));
    float Pushed = SlideAlongAWall(run, CharacterGroundControl_Push);
    TEST_CHECK(run, (Pushed > (0.95f * Walk)) && (Pushed <= (Walk + 1e-3f)));
}

/* Runs a character with ground control @p control (acceleration 64, friction 16) along +X, then holds the keys along
 * +Z for @p seconds; returns its velocity. */
static Vector3 TurnAtARun(TestRun* run, CharacterGroundControl control, float seconds)
{
    Vector3 Velocity = { 0 };
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f });
        Override(run, &Scene, Id, CharacterSetting_GroundControl, PropertyValue_FromEnum((int32_t)control));
        OverrideFloat(run, &Scene, Id, CharacterSetting_Acceleration, 64.0f);
        OverrideFloat(run, &Scene, Id, CharacterSetting_GroundFriction, 16.0f);
        CharacterInput Input = CreateWalk((Vector3){ 1.0f, 0.0f, 0.0f });
        Input.IsRunning = true;
        SetInput(run, &Scene, Id, &Input);
        TickFor(&Scene, TICKS_PER_SECOND);
        Input.MoveDirection = (Vector3){ 0.0f, 0.0f, 1.0f };
        SetInput(run, &Scene, Id, &Input);
        TickFor(&Scene, (uint32_t)(seconds * (float)TICKS_PER_SECOND));
        Velocity = GetState(&Scene, Id)->Velocity;
    }
    Scene_Deconstruct(run, &Scene);
    return Velocity;
}

/* A quarter turn at a run: steering takes the old motion at the acceleration, pushing leaves more of it to friction
 * (it slides wider); either has turned within a fifth of a second. Letting go, friction stops a run in its own time. */
static void SteeringTurnsQuickerThanPushingAndFrictionStops(TestRun* run)
{
    float Run = CharacterSettings_CreateDefault().RunSpeed;
    Vector3 Steered = TurnAtARun(run, CharacterGroundControl_Steer, 0.05f);
    Vector3 Pushed = TurnAtARun(run, CharacterGroundControl_Push, 0.05f);
    TEST_CHECK(run, Steered.x < Pushed.x);
    Steered = TurnAtARun(run, CharacterGroundControl_Steer, 0.2f);
    Pushed = TurnAtARun(run, CharacterGroundControl_Push, 0.2f);
    TEST_CHECK(run, (fabsf(Steered.x) < 0.01f) && IsNear(Steered.z, Run, 0.01f));
    TEST_CHECK(run, (fabsf(Pushed.x) < (0.05f * Run)) && (Pushed.z > (0.95f * Run)));

    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f });
        OverrideFloat(run, &Scene, Id, CharacterSetting_GroundFriction, 16.0f);
        CharacterInput Input = CreateWalk((Vector3){ 1.0f, 0.0f, 0.0f });
        Input.IsRunning = true;
        SetInput(run, &Scene, Id, &Input);
        TickFor(&Scene, TICKS_PER_SECOND);
        Input = CharacterInput_CreateDefault();
        SetInput(run, &Scene, Id, &Input);
        // A run slowed at 16 m/s squared stops in run / 16 seconds.
        uint32_t Stopping = (uint32_t)((Run / 16.0f) * (float)TICKS_PER_SECOND);
        TickFor(&Scene, Stopping - 2U);
        TEST_CHECK(run, GetGroundSpeed(&Scene, Id) > 0.0f);
        TickFor(&Scene, 4U);
        TEST_CHECK(run, GetGroundSpeed(&Scene, Id) == 0.0f);
    }
    Scene_Deconstruct(run, &Scene);
}

/* The eyes go down fast at first and slow as they near the crouched height, there after the crouch time (a linear ease
 * looks mechanical). */
static void TheEyesEaseIntoACrouch(TestRun* run)
{
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f });
        TickFor(&Scene, 10U);
        CharacterInput Input = CharacterInput_CreateDefault();
        Input.WantsCrouch = true;
        SetInput(run, &Scene, Id, &Input);
        // Half the crouch time leaves a tenth of the way (a linear ease, half).
        TickFor(&Scene, TICKS_PER_SECOND / 10U);
        TEST_CHECK(run, IsNear(GetState(&Scene, Id)->CrouchAmount, 0.9f, 0.01f));
        // There at the crouch time (0.2 s, 96 ticks), give or take a tick of rounding.
        TickFor(&Scene, (TICKS_PER_SECOND / 10U) - 2U);
        TEST_CHECK(run, GetState(&Scene, Id)->CrouchAmount < 1.0f);
        TickFor(&Scene, 4U);
        TEST_CHECK(run, (GetState(&Scene, Id)->CrouchAmount == 1.0f)
            && (GetState(&Scene, Id)->EyeHeight == CharacterSettings_CreateDefault().CrouchEyeHeight));
    }
    Scene_Deconstruct(run, &Scene);
}


// Static functions: looking and using.
static void TheLookRayFindsTheNearestThingAndUsesIt(TestRun* run)
{
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        uint64_t Near = AddBox(run, &Scene, (Vector3){ 0.0f, 1.6f, 1.5f }, (Vector3){ 0.2f, 0.2f, 0.2f }, (Vector3){ 0 },
            TestBoxKind_Static);
        AddBox(run, &Scene, (Vector3){ 0.0f, 1.6f, 2.0f }, (Vector3){ 0.2f, 0.2f, 0.2f }, (Vector3){ 0 },
            TestBoxKind_Static);
        uint64_t Far = AddBox(run, &Scene, (Vector3){ 2.9f, 1.6f, 0.0f }, (Vector3){ 0.2f, 0.2f, 0.2f }, (Vector3){ 0 },
            TestBoxKind_Static);
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f });
        TickFor(&Scene, 10U);

        // Looking along +Z at eye height: the near box (its face 1.3 m away), not its own capsule.
        CharacterInput Input = CharacterInput_CreateDefault();
        Input.LookPitch = -0.015f;
        SetInput(run, &Scene, Id, &Input);
        WorldRayHit Hit;
        TEST_CHECK(run, WorldCharacters_FindLookTarget(Scene.Characters, Id, &Hit) && (Hit.Shape.ObjectId == Near));
        TEST_CHECK(run, IsNear(Hit.Distance, 1.3f, 0.01f));

        // Along +X the far box (its face 2.7 m away) is out of reach (2.5 m), until the reach grows.
        Input.LookYaw = 0.5f * PI;
        SetInput(run, &Scene, Id, &Input);
        TEST_CHECK(run, !WorldCharacters_FindLookTarget(Scene.Characters, Id, &Hit));
        OverrideFloat(run, &Scene, Id, CharacterSetting_InteractionReach, 3.0f);
        TEST_CHECK(run, WorldCharacters_FindLookTarget(Scene.Characters, Id, &Hit) && (Hit.Shape.ObjectId == Far));

        // A use press names it, once.
        Input.WantsUse = true;
        SetInput(run, &Scene, Id, &Input);
        TickFor(&Scene, 5U);
        EventCounts Counts = { 0 };
        TakeEvents(&Scene, Id, &Counts);
        TEST_CHECK(run, (Counts.Counts[WorldCharacterEventKind_Used] == 1U) && (Counts.LastUsedId == Far));

        // Its layers decide what it sees.
        TEST_CHECK_SUCCESS(run, WorldCharacterComponent_SetOverridden(GetCharacter(&Scene, Id),
            CharacterSetting_InteractionReach, false));
        Override(run, &Scene, Id, CharacterSetting_InteractionLayers,
            PropertyValue_FromEnumSet(COLLISION_LAYER_BIT(CollisionLayer_Interaction)));
        Input.LookYaw = 0.0f;
        SetInput(run, &Scene, Id, &Input);
        TEST_CHECK(run, !WorldCharacters_FindLookTarget(Scene.Characters, Id, &Hit));
    }
    Scene_Deconstruct(run, &Scene);
}


// Static functions: the asset.
/* Builds a character definition from @p json without registering it and returns the constructor's result (the
 * definition, if one was built, is destroyed). */
static Error BuildCharacterDefinition(TestAssetFixture* fixture, const char* json)
{
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
    GenericBuffer_AppendString(&Bytes, (const unsigned char*)json);
    AssetDefinition* Definition = NULL;
    Error Result = AssetManager_BuildDefinition(fixture->Manager, fixture->Types.Character, &Bytes,
        (const unsigned char*)u8"test", &Definition);
    if (Definition != NULL)
    {
        Definition->VTable->Destroy(Definition);
    }
    Memory_Free(Bytes._data);
    return Result;
}

static void ADefinitionReadsItsKeysAndRefusesTheRest(TestRun* run)
{
    typedef struct RefusedStruct
    {
        const char* Json;
        ErrorCode Code;
        const char* Fragment;
    } Refused;
    static const Refused Cases[] =
    {
        { "{\"name\": \"c\", \"walking_speed\": 1}", ErrorCode_InvalidAssetDefinition,
            "Unknown key \"walking_speed\" in character \"c\"" },
        { "{\"name\": \"c\", \"radius\": -1}", ErrorCode_InvalidJSON, "radius" },
        { "{\"name\": \"c\", \"radius\": 1.0, \"height\": 1.5}", ErrorCode_InvalidAssetDefinition,
            "height (1.5 m) must be at least twice the radius" },
        { "{\"name\": \"c\", \"crouch_height\": 2.0}", ErrorCode_InvalidAssetDefinition,
            "crouch_height (2 m) must not be above height" },
        { "{\"name\": \"c\", \"slope_limit\": 90}", ErrorCode_InvalidJSON, "slope_limit" },
        { "{\"name\": \"c\", \"collides_with\": [\"world\", \"nowhere\"]}", ErrorCode_InvalidJSON, "collides_with" },
        { "{\"name\": \"c\", \"can_fly\": \"yes\"}", ErrorCode_InvalidJSON, "can_fly" },
    };
    TestAssetFixture Fixture;
    if (TestAssetFixture_Construct(run, &Fixture))
    {
        TEST_CHECK_SUCCESS(run, BuildCharacterDefinition(&Fixture, "{\"name\": \"c\"}"));
        for (size_t Index = 0U; Index < (sizeof(Cases) / sizeof(Cases[0])); Index++)
        {
            const char* const Fragments[] = { Cases[Index].Fragment, NULL };
            if (!TestText_CheckErrorMessage(run, BuildCharacterDefinition(&Fixture, Cases[Index].Json),
                Cases[Index].Code, Fragments))
            {
                printf("        the case was %s\n", Cases[Index].Json);
            }
        }
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

static void ADefinitionLoadsAndAPlacementUsesIt(TestRun* run)
{
    TestAssetFixture Fixture;
    if (!TestAssetFixture_Construct(run, &Fixture)
        || !TestAssets_AddCharacter(run, &Fixture, "{\"name\": \"dog\", \"radius\": 0.25, \"height\": 0.6, "
            "\"crouch_height\": 0.5, \"slope_limit\": 50, \"turn_speed\": 540, \"collides_with\": [\"world\", "
            "\"character\"], \"can_jump\": true}"))
    {
        TestAssetFixture_Deconstruct(run, &Fixture);
        return;
    }
    // The asset: degrees in JSON, radians in memory; layer names as a mask.
    CharacterAsset* Dog = NULL;
    AssetUserID User = ASSET_USER_ID_INVALID;
    if (TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(Fixture.Manager, &User))
        && TEST_CHECK_SUCCESS(run, AssetManager_LoadCharacter(Fixture.Manager, (const unsigned char*)u8"dog", User, &Dog)))
    {
        const CharacterSettings* Settings = CharacterAsset_GetSettings(Dog);
        TEST_CHECK(run, (Settings->Radius == 0.25f) && (Settings->Height == 0.6f) && Settings->CanJump);
        TEST_CHECK(run, IsNear(Settings->SlopeLimit, 50.0f * DEG2RAD, 1e-6f)
            && IsNear(Settings->TurnSpeed, 3.0f * PI, 1e-5f));
        TEST_CHECK(run, Settings->CollidesWith
            == (COLLISION_LAYER_BIT(CollisionLayer_World) | COLLISION_LAYER_BIT(CollisionLayer_Character)));
        TEST_CHECK(run, Settings->WalkSpeed == CharacterSettings_CreateDefault().WalkSpeed);
    }

    // A placement in a world with assets binds it, overrides it, swaps it and loses it.
    World TestWorld;
    KeyRegistry Keys;
    if (TestWorld_Construct(run, &TestWorld, &Keys, Fixture.Manager, NULL))
    {
        WorldObject* Object = NULL;
        WorldComponent* Component = NULL;
        if (TEST_CHECK_SUCCESS(run, World_CreateObject(&TestWorld, (const unsigned char*)u8"rex", &Object))
            && TEST_CHECK_SUCCESS(run, World_AddComponent(&TestWorld, WorldObject_GetId(Object),
                WorldComponentType_Character, &Component)))
        {
            WorldCharacterComponent* Character = WorldCharacterComponent_FromComponent(Component);
            TEST_CHECK(run, WorldCharacterComponent_GetBoundCharacter(Character) == NULL);
            TEST_CHECK_SUCCESS(run, WorldCharacterComponent_SetCharacter(Character, (const unsigned char*)u8"dog"));
            TEST_CHECK(run, WorldCharacterComponent_GetBoundCharacter(Character) == Dog);
            TEST_CHECK(run, WorldCharacterComponent_GetSettings(Character).Radius == 0.25f);
            PropertyValue Speed = PropertyValue_FromFloat(6.0f);
            TEST_CHECK_SUCCESS(run, WorldCharacterComponent_OverrideSetting(Character, CharacterSetting_RunSpeed,
                &Speed));
            CharacterSettings Resolved = WorldCharacterComponent_GetSettings(Character);
            TEST_CHECK(run, (Resolved.RunSpeed == 6.0f) && (Resolved.Radius == 0.25f));
            TEST_CHECK_SUCCESS(run, WorldCharacterComponent_SetCharacter(Character, (const unsigned char*)u8"missing"));
            TEST_CHECK(run, WorldCharacterComponent_GetBoundCharacter(Character) == NULL);
            Resolved = WorldCharacterComponent_GetSettings(Character);
            TEST_CHECK(run, (Resolved.RunSpeed == 6.0f) && (Resolved.Radius == CHARACTER_DEFAULT_RADIUS));
            TEST_CHECK_ERROR(run, WorldCharacterComponent_SetCharacter(Character, (const unsigned char*)u8"Not A Name"),
                ErrorCode_ArgumentOutOfRange);
        }
        TestWorld_Deconstruct(run, &TestWorld, &Keys);
    }
    if (User != ASSET_USER_ID_INVALID)
    {
        TEST_CHECK_SUCCESS(run, AssetManager_ReleaseAllAssetsForUser(Fixture.Manager, User));
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

static void TheShippedCharactersAreRead(TestRun* run)
{
    TestAssetFixture Fixture;
    GenericBuffer Root;
    GenericBuffer_AllocateVariable(&Root, 256U, sizeof(unsigned char));
    AssetUserID User = ASSET_USER_ID_INVALID;
    if (TestAssetFixture_Construct(run, &Fixture)
        && TEST_CHECK_SUCCESS(run, TestRun_BuildRepositoryPath(run, "compile/out/asset", &Root))
        && TEST_CHECK_SUCCESS(run, AssetManager_AddSearchRoot(Fixture.Manager, Root._data))
        && TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(Fixture.Manager, &User)))
    {
        size_t FailedCount = 1U;
        TEST_CHECK_SUCCESS(run, AssetManager_ReadDefinitions(Fixture.Manager, &FailedCount));
        TEST_CHECK(run, FailedCount == 0U);
        CharacterAsset* Human = NULL;
        CharacterAsset* Dog = NULL;
        CharacterAsset* Pigeon = NULL;
        if (TEST_CHECK_SUCCESS(run, AssetManager_LoadCharacter(Fixture.Manager, (const unsigned char*)u8"test_human",
                User, &Human))
            && TEST_CHECK_SUCCESS(run, AssetManager_LoadCharacter(Fixture.Manager, (const unsigned char*)u8"test_dog",
                User, &Dog))
            && TEST_CHECK_SUCCESS(run, AssetManager_LoadCharacter(Fixture.Manager, (const unsigned char*)u8"test_pigeon",
                User, &Pigeon)))
        {
            TEST_CHECK(run, CharacterAsset_GetSettings(Human)->CanJump && !CharacterAsset_GetSettings(Human)->CanFly);
            TEST_CHECK(run, (CharacterAsset_GetSettings(Dog)->Radius == 0.28f)
                && (CharacterAsset_GetSettings(Dog)->RunSpeed == 6.0f));
            TEST_CHECK(run, CharacterAsset_GetSettings(Pigeon)->CanFly && !CharacterAsset_GetSettings(Pigeon)->CanCrouch
                && (CharacterAsset_GetSettings(Pigeon)->Mass == 0.35f));
            TEST_CHECK_SUCCESS(run, CharacterSettings_CheckBody(CharacterAsset_GetSettings(Dog)));
            TEST_CHECK_SUCCESS(run, CharacterSettings_CheckBody(CharacterAsset_GetSettings(Pigeon)));
        }
        TEST_CHECK_SUCCESS(run, AssetManager_ReleaseAllAssetsForUser(Fixture.Manager, User));
    }
    Memory_Free(Root._data);
    TestAssetFixture_Deconstruct(run, &Fixture);
}


// Static functions: the collision filter's character options.
static void TheFilterSkipsPushablesAndWhatIgnoresTheLayer(TestRun* run)
{
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        uint64_t Wall = AddBox(run, &Scene, (Vector3){ 3.0f, 1.0f, 0.0f }, (Vector3){ 0.1f, 1.0f, 1.0f }, (Vector3){ 0 },
            TestBoxKind_Static);
        uint64_t Crate = AddBox(run, &Scene, (Vector3){ 1.5f, 1.0f, 0.0f }, (Vector3){ 0.2f, 0.2f, 0.2f },
            (Vector3){ 0 }, TestBoxKind_Pushable);
        uint64_t Ghost = AddBox(run, &Scene, (Vector3){ 0.0f, 1.0f, 2.0f }, (Vector3){ 0.2f, 0.2f, 0.2f },
            (Vector3){ 0 }, TestBoxKind_Static);
        WorldComponent* Component = NULL;
        if (World_TryGetComponent(&Scene.World, Ghost, WorldComponentType_Collider, &Component))
        {
            WorldColliderComponent* Collider = WorldColliderComponent_FromComponent(Component);
            TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetCollidesWith(Collider,
                COLLISION_LAYER_BIT(CollisionLayer_World)));
            TEST_CHECK_SUCCESS(run, WorldColliderComponent_SetOverridden(Collider, WorldColliderSetting_CollidesWith,
                true));
        }
        WorldCollision_Sync(Scene.Collision);
        Vector3Double Origin = TestWorld_At((Vector3){ 0.0f, 1.0f, 0.0f });
        WorldRayHit Hit;
        WorldCollisionFilter Filter = WorldCollisionFilter_Create(COLLISION_LAYER_MASK_ALL,
            WorldCollisionTriggerMode_Ignore);
        TEST_CHECK(run, WorldCollision_Raycast(Scene.Collision, Origin, (Vector3){ 1.0f, 0.0f, 0.0f }, 10.0f, &Filter,
            &Hit) && (Hit.Shape.ObjectId == Crate));
        Filter.ArePushablesIgnored = true;
        TEST_CHECK(run, WorldCollision_Raycast(Scene.Collision, Origin, (Vector3){ 1.0f, 0.0f, 0.0f }, 10.0f, &Filter,
            &Hit) && (Hit.Shape.ObjectId == Wall));
        // The ghost's mask lacks the character layer: a query colliding as a character passes it.
        TEST_CHECK(run, WorldCollision_Raycast(Scene.Collision, Origin, (Vector3){ 0.0f, 0.0f, 1.0f }, 10.0f, &Filter,
            &Hit) && (Hit.Shape.ObjectId == Ghost));
        Filter.HasOwnLayer = true;
        Filter.OwnLayer = CollisionLayer_Character;
        TEST_CHECK(run, !WorldCollision_Raycast(Scene.Collision, Origin, (Vector3){ 0.0f, 0.0f, 1.0f }, 10.0f, &Filter,
            &Hit));
        Filter.OwnLayer = CollisionLayer_World;
        TEST_CHECK(run, WorldCollision_Raycast(Scene.Collision, Origin, (Vector3){ 0.0f, 0.0f, 1.0f }, 10.0f, &Filter,
            &Hit) && (Hit.Shape.ObjectId == Ghost));
    }
    Scene_Deconstruct(run, &Scene);
}


// Static functions: the player controller.
/* One tick of input with @p keys held (pressed this tick when @p isPressed) and the mouse moved by @p mouse. */
static void BeginInputTick(GameInput* input, const GameKey* keys, size_t keyCount, bool isPressed, Vector2 mouse)
{
    GameInputSample Sample;
    GameInputSample_Clear(&Sample);
    for (size_t Index = 0U; Index < keyCount; Index++)
    {
        Sample.KeyStates[keys[Index]] = (uint8_t)(GAME_INPUT_KEY_STATE_DOWN
            | (isPressed ? GAME_INPUT_KEY_STATE_PRESSED : 0U));
    }
    Sample.MouseDelta = mouse;
    Sample.IsCursorCaptured = true;
    GameInput_BeginTick(input, &Sample);
}

static void ThePlayerControllerFillsTheCharactersInput(TestRun* run)
{
    CharacterScene Scene;
    GameInput Input;
    if (!TEST_CHECK_SUCCESS(run, GameInput_Construct(&Input)))
    {
        return;
    }
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f });
        WorldCharacterComponent* Character = GetCharacter(&Scene, Id);
        PlayerController Controller;
        PlayerController_Construct(&Controller);
        TEST_CHECK_SUCCESS(run, PlayerController_SetCharacter(&Controller, &Scene.World, Id, 0.5f, 0.1f));
        TEST_CHECK(run, (PlayerController_GetCharacter(&Controller) == Id)
            && (WorldCharacterComponent_GetInput(Character)->Facing == CharacterFacing_Look));

        // W and the mouse: forward along the turned look.
        GameKey Forward[] = { GameKey_W };
        BeginInputTick(&Input, Forward, 1U, true, (Vector2){ 10.0f, -4.0f });
        PlayerController_Update(&Controller, &Input, &Scene.World, true);
        const CharacterInput* Driven = WorldCharacterComponent_GetInput(Character);
        float Yaw = 0.5f - (10.0f * PLAYER_CONTROLLER_DEFAULT_MOUSE_SENSITIVITY);
        TEST_CHECK(run, IsNear(Driven->LookYaw, Yaw, 1e-6f)
            && IsNear(Driven->LookPitch, 0.1f + (4.0f * PLAYER_CONTROLLER_DEFAULT_MOUSE_SENSITIVITY), 1e-6f));
        TEST_CHECK(run, IsNear(Driven->MoveDirection.x, sinf(Yaw), 1e-5f) && IsNear(Driven->MoveDirection.z, cosf(Yaw),
            1e-5f) && (Driven->MoveDirection.y == 0.0f));
        // Without mouse look the mouse does nothing; D strafes to the right of the look (toward -X at yaw 0).
        GameKey Strafe[] = { GameKey_D };
        TEST_CHECK_SUCCESS(run, PlayerController_SetCharacter(&Controller, &Scene.World, Id, 0.0f, 0.0f));
        BeginInputTick(&Input, Strafe, 1U, true, (Vector2){ 50.0f, 0.0f });
        PlayerController_Update(&Controller, &Input, &Scene.World, false);
        TEST_CHECK(run, (Driven->LookYaw == 0.0f) && IsNear(Driven->MoveDirection.x, -1.0f, 1e-6f));

        // E uses, and nothing below the controller sees the press.
        GameKey Use[] = { GameKey_E };
        BeginInputTick(&Input, Use, 1U, true, (Vector2){ 0 });
        PlayerController_Update(&Controller, &Input, &Scene.World, true);
        TEST_CHECK(run, Driven->WantsUse && !GameInput_IsKeyPressed(&Input, GameKey_E));

        // Crouching held, then toggled.
        GameKey Crouch[] = { GameKey_LeftControl };
        BeginInputTick(&Input, Crouch, 1U, true, (Vector2){ 0 });
        PlayerController_Update(&Controller, &Input, &Scene.World, true);
        TEST_CHECK(run, Driven->WantsCrouch);
        BeginInputTick(&Input, NULL, 0U, false, (Vector2){ 0 });
        PlayerController_Update(&Controller, &Input, &Scene.World, true);
        TEST_CHECK(run, !Driven->WantsCrouch);
        Controller.IsCrouchToggled = true;
        BeginInputTick(&Input, Crouch, 1U, true, (Vector2){ 0 });
        PlayerController_Update(&Controller, &Input, &Scene.World, true);
        BeginInputTick(&Input, NULL, 0U, false, (Vector2){ 0 });
        PlayerController_Update(&Controller, &Input, &Scene.World, true);
        TEST_CHECK(run, Driven->WantsCrouch);
        BeginInputTick(&Input, Crouch, 1U, true, (Vector2){ 0 });
        PlayerController_Update(&Controller, &Input, &Scene.World, true);
        TEST_CHECK(run, !Driven->WantsCrouch);

        // Flying: Space climbs.
        OverrideBool(run, &Scene, Id, CharacterSetting_CanFly, true);
        Controller.WantsToFly = true;
        BeginInputTick(&Input, NULL, 0U, false, (Vector2){ 0 });
        PlayerController_Update(&Controller, &Input, &Scene.World, true);
        Tick(&Scene);
        TEST_CHECK(run, GetState(&Scene, Id)->Mode == CharacterMovementMode_Flying);
        GameKey Climb[] = { GameKey_Space };
        BeginInputTick(&Input, Climb, 1U, true, (Vector2){ 0 });
        PlayerController_Update(&Controller, &Input, &Scene.World, true);
        TEST_CHECK(run, IsNear(Driven->MoveDirection.y, 1.0f, 1e-6f) && !Driven->WantsJump);

        // The camera sits at the eyes and looks where the character looks.
        GameCamera View = GameCamera_Create(Vector3Double_Zero(), 60.0f);
        PlayerController_ApplyCamera(&Controller, &Scene.World, &View);
        Vector3Double Eye = WorldCharacterComponent_GetEyePosition(Character);
        TEST_CHECK(run, Vector3Double_EqualsClose(View.Position, Eye, 1e-6) && (View.Yaw == Driven->LookYaw));

        // Letting go.
        TEST_CHECK_SUCCESS(run, PlayerController_SetCharacter(&Controller, &Scene.World, 0U, 0.0f, 0.0f));
        TEST_CHECK(run, PlayerController_GetCharacter(&Controller) == 0U);
        PlayerController_Deconstruct(&Controller);
    }
    Scene_Deconstruct(run, &Scene);
    TEST_CHECK_SUCCESS(run, GameInput_Deconstruct(&Input));
}

static void ThePlayersLookTargetReportsItsChanges(TestRun* run)
{
    CharacterScene Scene;
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        uint64_t Box = AddBox(run, &Scene, (Vector3){ 0.0f, 1.6f, 1.5f }, (Vector3){ 0.2f, 0.2f, 0.2f }, (Vector3){ 0 },
            TestBoxKind_Static);
        uint64_t Id = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f });
        TickFor(&Scene, 5U);
        PlayerController Controller;
        PlayerController_Construct(&Controller);
        TEST_CHECK_SUCCESS(run, PlayerController_SetCharacter(&Controller, &Scene.World, Id, 0.0f, 0.0f));
        WorldRayHit Hit;
        TEST_CHECK(run, PlayerController_UpdateLookTarget(&Controller, Scene.Characters));
        TEST_CHECK(run, PlayerController_GetLookTarget(&Controller, &Hit) && (Hit.Shape.ObjectId == Box));
        TEST_CHECK(run, !PlayerController_UpdateLookTarget(&Controller, Scene.Characters));
        TEST_CHECK_SUCCESS(run, PlayerController_SetCharacter(&Controller, &Scene.World, Id, PI, 0.0f));
        TEST_CHECK(run, !PlayerController_UpdateLookTarget(&Controller, Scene.Characters));
        TEST_CHECK(run, !PlayerController_GetLookTarget(&Controller, &Hit));
        PlayerController_Deconstruct(&Controller);
    }
    Scene_Deconstruct(run, &Scene);
}


/* The character a player controller lets go of drops the input it last had (keeping it, it would walk on forever). */
static void TheCharacterLetGoOfStopsWalking(TestRun* run)
{
    CharacterScene Scene;
    GameInput Input;
    if (!TEST_CHECK_SUCCESS(run, GameInput_Construct(&Input)))
    {
        return;
    }
    if (Scene_Construct(run, &Scene))
    {
        AddFloor(run, &Scene);
        uint64_t First = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.0f, 0.0f });
        uint64_t Second = AddCharacter(run, &Scene, (Vector3){ 0.0f, 0.0f, 5.0f });
        PlayerController Controller;
        PlayerController_Construct(&Controller);
        TEST_CHECK_SUCCESS(run, PlayerController_SetCharacter(&Controller, &Scene.World, First, 0.0f, 0.0f));
        GameKey Forward[] = { GameKey_W };
        BeginInputTick(&Input, Forward, 1U, true, (Vector2){ 0 });
        PlayerController_Update(&Controller, &Input, &Scene.World, false);
        TickFor(&Scene, TICKS_PER_SECOND / 2U);
        TEST_CHECK(run, GetState(&Scene, First)->Velocity.z > 1.0f);
        // Taking another character lets go of the first.
        TEST_CHECK_SUCCESS(run, PlayerController_SetCharacter(&Controller, &Scene.World, Second, 0.0f, 0.0f));
        TickFor(&Scene, TICKS_PER_SECOND);
        Vector3 Stopped = GetFeet(&Scene, First);
        TickFor(&Scene, TICKS_PER_SECOND);
        TEST_CHECK(run, Vector3Distance(GetFeet(&Scene, First), Stopped) < 1e-4f);
        const CharacterInput* Left = WorldCharacterComponent_GetInput(GetCharacter(&Scene, First));
        TEST_CHECK(run, Vector3Equals(Left->MoveDirection, (Vector3){ 0.0f, 0.0f, 0.0f })
            && (Left->Facing == CharacterFacing_Keep));
        PlayerController_Deconstruct(&Controller);
    }
    Scene_Deconstruct(run, &Scene);
    TEST_CHECK_SUCCESS(run, GameInput_Deconstruct(&Input));
}


// Public functions.
void WorldCharacterTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "the_tables_pass_their_checks_and_share_their_rows", TheTablesPassTheirChecksAndShareTheirRows },
        { "every_override_resolves", EveryOverrideResolves },
        { "the_body_rules_hold_across_settings", TheBodyRulesHoldAcrossSettings },
        { "the_input_is_checked_and_normalised", TheInputIsCheckedAndNormalised },
        { "a_character_gets_a_capsule_collider_and_stands_still", ACharacterGetsACapsuleColliderAndStandsStill },
        { "a_standing_character_keeps_its_feet_2_mm_up_without_drifting",
            AStandingCharacterKeepsItsFeet2MmUpWithoutDrifting },
        { "a_square_walked_far_away_ends_where_it_ends_at_the_origin", ASquareWalkedFarAwayEndsWhereItEndsAtTheOrigin },
        { "walking_into_a_wall_stops_just_short_of_it", WalkingIntoAWallStopsJustShortOfIt },
        { "walking_diagonally_slides_along_a_wall", WalkingDiagonallySlidesAlongAWall },
        { "an_inside_corner_stops_without_jitter", AnInsideCornerStopsWithoutJitter },
        { "a_low_step_is_climbed_and_a_high_one_is_not", ALowStepIsClimbedAndAHighOneIsNot },
        { "a_gentle_slope_is_walked_and_a_steep_one_is_not", AGentleSlopeIsWalkedAndASteepOneIsNot },
        { "walking_down_stairs_falls_off_each_step_unless_it_steps_down",
            WalkingDownStairsFallsOffEachStepUnlessItStepsDown },
        { "stepping_off_a_ledge_falls_and_lands", SteppingOffALedgeFallsAndLands },
        { "a_moving_platform_carries_a_character", AMovingPlatformCarriesACharacter },
        { "a_turning_platform_turns_the_character", ATurningPlatformTurnsTheCharacter },
        { "two_characters_walking_into_each_other_stop", TwoCharactersWalkingIntoEachOtherStop },
        { "a_heavy_character_pushes_a_light_one", AHeavyCharacterPushesALightOne },
        { "a_crouched_character_passes_under_a_beam_and_stands_up_past_it",
            ACrouchedCharacterPassesUnderABeamAndStandsUpPastIt },
        { "a_jump_reaches_its_height_only_when_allowed", AJumpReachesItsHeightOnlyWhenAllowed },
        { "late_and_early_jumps_count", LateAndEarlyJumpsCount },
        { "a_flyer_climbs_slides_under_a_ceiling_and_lands_when_it_stops",
            AFlyerClimbsSlidesUnderACeilingAndLandsWhenItStops },
        { "a_character_that_may_not_fly_stays_on_its_feet", ACharacterThatMayNotFlyStaysOnItsFeet },
        { "nothing_tunnels_through_thin_walls_or_floors", NothingTunnelsThroughThinWallsOrFloors },
        { "a_character_nobody_drives_falls_and_rides", ACharacterNobodyDrivesFallsAndRides },
        { "settings_changed_at_runtime_take_effect", SettingsChangedAtRuntimeTakeEffect },
        { "an_anchor_slides_the_character_there_and_faces_its_way", AnAnchorSlidesTheCharacterThereAndFacesItsWay },
        { "a_wall_on_the_way_fails_the_approach", AWallOnTheWayFailsTheApproach },
        { "locks_keep_the_character_from_moving_or_turning", LocksKeepTheCharacterFromMovingOrTurning },
        { "the_look_ray_finds_the_nearest_thing_and_uses_it", TheLookRayFindsTheNearestThingAndUsesIt },
        { "a_definition_reads_its_keys_and_refuses_the_rest", ADefinitionReadsItsKeysAndRefusesTheRest },
        { "a_definition_loads_and_a_placement_uses_it", ADefinitionLoadsAndAPlacementUsesIt },
        { "the_shipped_characters_are_read", TheShippedCharactersAreRead },
        { "the_filter_skips_pushables_and_what_ignores_the_layer", TheFilterSkipsPushablesAndWhatIgnoresTheLayer },
        { "the_player_controller_fills_the_characters_input", ThePlayerControllerFillsTheCharactersInput },
        { "the_players_look_target_reports_its_changes", ThePlayersLookTargetReportsItsChanges },
        { "a_riser_is_climbed_wherever_the_tick_meets_it", ARiserIsClimbedWhereverTheTickMeetsIt },
        { "a_character_dropped_onto_a_crate_or_a_head_lands_on_it", ACharacterDroppedOntoACrateOrAHeadLandsOnIt },
        { "a_fast_move_into_a_plane_keeps_its_slide", AFastMoveIntoAPlaneKeepsItsSlide },
        { "a_jump_off_a_moving_lift_keeps_the_lifts_velocity", AJumpOffAMovingLiftKeepsTheLiftsVelocity },
        { "no_ground_snap_still_stands_on_flat_ground", NoGroundSnapStillStandsOnFlatGround },
        { "a_carried_part_pushes_the_crate_it_hangs_from", ACarriedPartPushesTheCrateItHangsFrom },
        { "a_step_height_limits_the_edges_the_round_bottom_rolls_over",
            AStepHeightLimitsTheEdgesTheRoundBottomRollsOver },
        { "a_reenabled_character_is_not_thrown_by_what_moved_meanwhile",
            AReenabledCharacterIsNotThrownByWhatMovedMeanwhile },
        { "a_teleport_gives_back_the_late_jump", ATeleportGivesBackTheLateJump },
        { "a_teleport_refuses_places_the_world_cannot_hold", ATeleportRefusesPlacesTheWorldCannotHold },
        { "the_character_let_go_of_stops_walking", TheCharacterLetGoOfStopsWalking },
        { "a_run_along_a_turned_wall_never_stalls_and_nothing_pushes_the_walker",
            ARunAlongATurnedWallNeverStallsAndNothingPushesTheWalker },
        { "walking_off_a_block_rolls_a_little_then_falls", WalkingOffABlockRollsALittleThenFalls },
        { "running_off_a_ledge_lands_as_from_its_top", RunningOffALedgeLandsAsFromItsTop },
        { "running_off_a_ramps_end_keeps_its_descent", RunningOffARampsEndKeepsItsDescent },
        { "a_held_jump_jumps_again_once_the_delay_has_passed", AHeldJumpJumpsAgainOnceTheDelayHasPassed },
        { "a_sprint_jump_adds_its_boost_without_adding_up", ASprintJumpAddsItsBoostWithoutAddingUp },
        { "a_sprint_jump_up_a_slope_lands_on_it", ASprintJumpUpASlopeLandsOnIt },
        { "the_air_keys_steer_and_brake_but_never_speed_up", TheAirKeysSteerAndBrakeButNeverSpeedUp },
        { "a_fall_reaches_the_airs_terminal_speed", AFallReachesTheAirsTerminalSpeed },
        { "both_ground_controls_slide_along_a_wall_no_faster_than_they_walk",
            BothGroundControlsSlideAlongAWallNoFasterThanTheyWalk },
        { "steering_turns_quicker_than_pushing_and_friction_stops", SteeringTurnsQuickerThanPushingAndFrictionStops },
        { "the_eyes_ease_into_a_crouch", TheEyesEaseIntoACrouch },
    };
    TestWorld_RunSuite(run, "WorldCharacter", "WorldCharacterFar", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
