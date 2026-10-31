// The world test frame's characters (WorldTestCharacters.h): walk mode, the test NPCs and their scripted brains,
// the character events, the look target, the status line, and the humans' bodies and their actions. Test content
// only.

#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "WorldTestCharacters.h"
#include "DataMap.h"
#include "DataValue.h"
#include "NamespacedKey.h"
#include "WorldCharacterComponent.h"
#include "WorldModelComponent.h"
#include "WorldObject.h"
#include "GameMath.h"
#include "raylib/raymath.h"
#include "wr/WRMemory.h"


// Macros.
/** The kinds of character the player and the NPCs take (the test character assets, characters/test_world/). */
#define TEST_KIND_COUNT 3U
#define TEST_KIND_HUMAN 0U
#define TEST_KIND_DOG 1U
#define TEST_KIND_PIGEON 2U
/** How far in front of the camera the NPCs spawn, and how far apart, metres. */
#define NPC_SPAWN_DISTANCE 3.0f
#define NPC_SPACING 1.5f
/** How far down the ground is looked for under a spawn, metres. */
#define GROUND_SEARCH_DISTANCE 100.0f
/** The test cube's size (models/test: half a metre a side), which the placeholder bodies scale. */
#define TEST_CUBE_SIZE 0.5f
/** The human NPC: metres a leg of its walk lasts, in seconds, and every how many legs it crouches. */
#define HUMAN_LEG_SECONDS 3.0f
#define HUMAN_CROUCH_EVERY 3U
/** The dog: the radius it runs around, how long it runs, how long it lies down. */
#define DOG_CIRCLE_RADIUS 2.0f
#define DOG_RUN_SECONDS 5.0f
#define DOG_REST_SECONDS 2.0f
/** The pigeon: how long it walks, climbs and circles, how far around and how high. */
#define PIGEON_WALK_SECONDS 3.0f
#define PIGEON_CLIMB_SECONDS 1.2f
#define PIGEON_CIRCLE_SECONDS 5.0f
#define PIGEON_CIRCLE_RADIUS 3.0f
/** A human's body: the placeholder humanoid's world model, whose animator plays its graph. */
#define HUMAN_WORLD_MODEL ((const unsigned char*)u8"test_humanoid")
/** The data key under which an object offers an action to whoever uses it (text: the action's name), the test world's
 *  bench, and the socket a human holds things in. */
#define ACTION_DATA_KEY ((const unsigned char*)u8"tbp:action")
#define BENCH_OBJECT_NAME ((const unsigned char*)u8"bench")
#define HAND_SOCKET "hand_r"
/** The NPC human heading for the bench: how far in front of it it aims, and how near that point it sits down. */
#define BENCH_APPROACH_DISTANCE 1.0
#define BENCH_START_DISTANCE 0.4
/** How far down a released object looks for something to rest on, metres. */
#define SETTLE_SEARCH_DISTANCE 3.0f


// Types.
/* A kind's placeholder body: a box for the trunk and a small one at the front (the nose or head), as fractions of the
 * test cube; heights are the body's when standing (crouching squashes the trunk). */
typedef struct TestBodyStruct
{
    Vector3 TrunkCenter;
    Vector3 TrunkSize;
    Vector3 NoseCenter;
    Vector3 NoseSize;
    Color TrunkColor;
    Color NoseColor;
} TestBody;

/* The pigeon's pattern. */
typedef enum PigeonPhaseEnum
{
    PigeonPhase_Walk = 0,
    PigeonPhase_Climb = 1,
    PigeonPhase_Circle = 2,
    PigeonPhase_Descend = 3
} PigeonPhase;


// Fields.
static const char* const KindAssets[TEST_KIND_COUNT] = { "test_human", "test_dog", "test_pigeon" };
static const char* const KindNames[TEST_KIND_COUNT] = { "human", "dog", "pigeon" };
static const char* const NpcNames[WORLD_TEST_NPC_COUNT] = { "npc_human", "npc_dog", "npc_pigeon" };

static const TestBody Bodies[TEST_KIND_COUNT] =
{
    // A human: 0.5 by 1.7 by 0.3 m in orange, a dark nose at the face.
    { { 0.0f, 0.85f, 0.0f }, { 0.5f, 1.7f, 0.3f }, { 0.0f, 1.55f, 0.18f }, { 0.12f, 0.1f, 0.12f },
        { 230, 140, 60, 255 }, { 90, 40, 20, 255 } },
    // A dog: 0.3 wide, 0.4 tall, 0.75 long on 0.1 m of legs in brown, its head forward.
    { { 0.0f, 0.3f, 0.0f }, { 0.3f, 0.4f, 0.75f }, { 0.0f, 0.5f, 0.42f }, { 0.2f, 0.2f, 0.2f },
        { 140, 90, 50, 255 }, { 70, 45, 25, 255 } },
    // A pigeon: 0.12 by 0.15 by 0.22 m in grey-violet, its head forward.
    { { 0.0f, 0.1f, 0.0f }, { 0.12f, 0.15f, 0.22f }, { 0.0f, 0.22f, 0.09f }, { 0.08f, 0.08f, 0.08f },
        { 130, 130, 160, 255 }, { 60, 90, 70, 255 } },
};


// Static functions: logging.
static void Log(WorldTestCharacters* self, const char* format, ...)
{
    if (self->_logger == NULL)
    {
        return;
    }
    char Text[512];
    va_list Arguments;
    va_start(Arguments, format);
    int Length = vsnprintf(Text, sizeof(Text), format, Arguments);
    va_end(Arguments);
    if (Length < 0)
    {
        return;
    }
    Error LogResult = Logger_LogInfoFormatted(self->_logger, (const unsigned char*)u8"WorldTest: %s", Text);
    Error_Deconstruct(&LogResult);
}

static const char* GetName(World* world, uint64_t id)
{
    WorldObject* Object = NULL;
    const unsigned char* Name = World_TryGetObject(world, id, &Object) ? WorldObject_GetName(Object) : NULL;
    return (Name != NULL) ? (const char*)Name : "";
}

static WorldCharacterComponent* FindCharacter(World* world, uint64_t id)
{
    WorldComponent* Component = NULL;
    return ((id != 0U) && World_TryGetComponent(world, id, WorldComponentType_Character, &Component))
        ? WorldCharacterComponent_FromComponent(Component) : NULL;
}

/* Logs a failed call once it happened (test content: nothing else to do about it). */
static bool Check(WorldTestCharacters* self, Error result, const char* what)
{
    if (result.Code == ErrorCode_Success)
    {
        return true;
    }
    Log(self, "%s failed: %s", what, (result.Message != NULL) ? (const char*)result.Message : "no details");
    Error_Deconstruct(&result);
    return false;
}


// Static functions: spawning.
/* The ground's height under a point (world, prop, vehicle and dynamic layers; characters are skipped), or @p fallback. */
static double FindGroundHeight(WorldTestCharacters* self, Vector3Double above, double fallback)
{
    uint32_t Mask = COLLISION_LAYER_BIT(CollisionLayer_World) | COLLISION_LAYER_BIT(CollisionLayer_Prop)
        | COLLISION_LAYER_BIT(CollisionLayer_Vehicle) | COLLISION_LAYER_BIT(CollisionLayer_Dynamic);
    WorldCollisionFilter Filter = WorldCollisionFilter_Create(Mask, WorldCollisionTriggerMode_Ignore);
    WorldRayHit Hit;
    if (WorldCollision_Raycast(self->_collision, above, (Vector3){ 0.0f, -1.0f, 0.0f }, GROUND_SEARCH_DISTANCE, &Filter,
        &Hit) && !Hit.IsStartInside)
    {
        return Hit.Point.Y;
    }
    return fallback;
}

/* A child box of the test model under @p parent, at @p center (in the parent's space), @p size metres, tinted
 * @p color. */
static uint64_t AddBox(WorldTestCharacters* self, uint64_t parent, const char* name, Vector3 center, Vector3 size,
    Color color)
{
    WorldObject* Object = NULL;
    if (!Check(self, World_CreateObject(self->_world, (const unsigned char*)name, &Object), "creating a body part"))
    {
        return 0U;
    }
    uint64_t Id = WorldObject_GetId(Object);
    Vector3 Scale = Vector3Scale(size, 1.0f / TEST_CUBE_SIZE);
    Check(self, WorldObject_SetPosition(Object, GameMath_FromVector3(center)), "placing a body part");
    Check(self, WorldObject_SetScale(Object, Scale), "sizing a body part");
    Check(self, WorldObject_SetTint(Object, (RenderColor){ .Tint = color, .Brightness = 1.0f, .Opacity = 1.0f }),
        "colouring a body part");
    Check(self, World_SetParent(self->_world, Id, parent, false), "attaching a body part");
    WorldComponent* Component = NULL;
    if (Check(self, World_AddComponent(self->_world, Id, WorldComponentType_Model, &Component), "adding a body model"))
    {
        WorldModelComponent* Body = WorldModelComponent_FromComponent(Component);
        Check(self, WorldModelComponent_SetWorldModel(Body, (const unsigned char*)u8"test"), "naming the body model");
        // An animal keeps no sky off the ground under it (its shade is the AO's and the shadows'), as the humanoids'
        // world models say for people.
        Check(self, WorldModelComponent_SetOverridden(Body, WorldModelSetting_OccludesSky, true),
            "overriding the body's sky");
        Check(self, WorldModelComponent_SetOccludesSky(Body, false), "keeping no sky out");
    }
    return Id;
}

/* A human's body: the placeholder humanoid on the character's own object, its animator playing the world model's
 * graph (the camera's passes leave its head out while it is the player's in first person). */
static void AddHumanBody(WorldTestCharacters* self, uint64_t id)
{
    WorldComponent* Component = NULL;
    if (Check(self, World_AddComponent(self->_world, id, WorldComponentType_Model, &Component), "adding a body"))
    {
        Check(self, WorldModelComponent_SetWorldModel(WorldModelComponent_FromComponent(Component), HUMAN_WORLD_MODEL),
            "naming the body");
    }
    Check(self, World_AddComponent(self->_world, id, WorldComponentType_Animator, &Component), "adding an animator");
}

static void DropHeld(WorldTestCharacters* self, uint64_t holderId);

/* Takes a human's body off (the action it was in ends with its animator); what its hand held is put down first (a
 * socket without a pose would be the holder's own space: the cup inside a dog). */
static void RemoveHumanBody(WorldTestCharacters* self, uint64_t id)
{
    DropHeld(self, id);
    WorldComponent* Component = NULL;
    if (World_TryGetComponent(self->_world, id, WorldComponentType_Animator, &Component))
    {
        Check(self, World_RemoveComponent(self->_world, id, WorldComponentType_Animator), "removing an animator");
    }
    if (World_TryGetComponent(self->_world, id, WorldComponentType_Model, &Component))
    {
        Check(self, World_RemoveComponent(self->_world, id, WorldComponentType_Model), "removing a body");
    }
}

/* A runtime character object (not saved with the world) of @p kind, its feet at @p feet facing @p yaw. A human wears
 * the humanoid body; another kind gets a placeholder body of boxes when @p bodyId is given (its trunk's id goes there,
 * 0 for a human). */
static uint64_t Spawn(WorldTestCharacters* self, const char* name, size_t kind, Vector3Double feet, float yaw,
    uint64_t* bodyId)
{
    WorldObject* Object = NULL;
    if (!Check(self, World_CreateObject(self->_world, (const unsigned char*)name, &Object), "creating a character"))
    {
        return 0U;
    }
    WorldObject_SetPersistent(Object, false);
    uint64_t Id = WorldObject_GetId(Object);
    WorldComponent* Component = NULL;
    if (!Check(self, World_AddComponent(self->_world, Id, WorldComponentType_Character, &Component),
        "adding a character"))
    {
        return Id;
    }
    Check(self, WorldCharacterComponent_SetCharacter(WorldCharacterComponent_FromComponent(Component),
        (const unsigned char*)KindAssets[kind]), "naming the character");
    if (kind == TEST_KIND_HUMAN)
    {
        AddHumanBody(self, Id);
        if (bodyId != NULL)
        {
            *bodyId = 0U;
        }
    }
    else if (bodyId != NULL)
    {
        const TestBody* Body = &Bodies[kind];
        *bodyId = AddBox(self, Id, "body", Body->TrunkCenter, Body->TrunkSize, Body->TrunkColor);
        AddBox(self, Id, "nose", Body->NoseCenter, Body->NoseSize, Body->NoseColor);
    }
    Check(self, WorldCharacters_Teleport(self->_characters, Id, feet, yaw), "placing a character");
    return Id;
}

/* Removes a test character (a runtime object), putting down what it holds first: its subtree goes with it, and a
 * world object in its hand (the coffee cup) would go too. */
static void RemoveObject(WorldTestCharacters* self, uint64_t* id)
{
    if (*id != 0U)
    {
        DropHeld(self, *id);
        Check(self, World_RemoveObject(self->_world, *id), "removing a character");
        *id = 0U;
    }
}


// Static functions: walk mode and the NPCs.
static void StartWalking(WorldTestCharacters* self, const GameCamera* camera)
{
    Vector3Double Above = camera->Position;
    Vector3Double Feet = Vector3Double_Create(Above.X, FindGroundHeight(self, Above, Above.Y) + 0.01, Above.Z);
    self->_playerId = Spawn(self, "player", self->_playerKind, Feet, camera->Yaw, NULL);
    if ((self->_playerId == 0U)
        || !Check(self, PlayerController_SetCharacter(&self->_controller, self->_world, self->_playerId, camera->Yaw,
            camera->Pitch), "controlling the player"))
    {
        RemoveObject(self, &self->_playerId);
        return;
    }
    self->_isWalking = true;
    Log(self, "walk mode: you are a %s (%s) at (%.2f, %.2f, %.2f). WASD walk, SHIFT run, SPACE jump, CTRL crouch, E use "
        "(sit on the bench, pick up the coffee cup, open the door; E again stands up), 9 put down, 0 wave; F1 back to the "
        "flycam, F3 fly, F4 another kind.", KindNames[self->_playerKind],
        KindAssets[self->_playerKind], Feet.X, Feet.Y, Feet.Z);
}

static void StopWalking(WorldTestCharacters* self)
{
    Check(self, PlayerController_SetCharacter(&self->_controller, self->_world, 0U, 0.0f, 0.0f), "releasing the player");
    RemoveObject(self, &self->_playerId);
    self->_isWalking = false;
    Log(self, "walk mode off: the flycam again.");
}

static void ToggleNpcs(WorldTestCharacters* self, const GameCamera* camera)
{
    if (self->_areNpcsSpawned)
    {
        for (size_t Index = 0U; Index < WORLD_TEST_NPC_COUNT; Index++)
        {
            RemoveObject(self, &self->_npcs[Index].Id);
            self->_npcs[Index].BodyId = 0U;
        }
        self->_areNpcsSpawned = false;
        Log(self, "the test NPCs are gone.");
        return;
    }
    Vector3 Forward = { sinf(camera->Yaw), 0.0f, cosf(camera->Yaw) };
    Vector3 Right = { -cosf(camera->Yaw), 0.0f, sinf(camera->Yaw) };
    Vector3Double Front = Vector3Double_AddScaled(camera->Position, GameMath_FromVector3(Forward), NPC_SPAWN_DISTANCE);
    Vector3Double Spawned[WORLD_TEST_NPC_COUNT];
    for (size_t Index = 0U; Index < WORLD_TEST_NPC_COUNT; Index++)
    {
        WorldTestNpc* Npc = &self->_npcs[Index];
        Vector3Double Above = Vector3Double_AddScaled(Front, GameMath_FromVector3(Right),
            (double)NPC_SPACING * ((double)Index - 1.0));
        Vector3Double Feet = Vector3Double_Create(Above.X, FindGroundHeight(self, Above, 0.0) + 0.01, Above.Z);
        Memory_Zero(Npc, sizeof(*Npc));
        Npc->Heading = camera->Yaw + PI;
        Npc->Center = Vector3Double_AddScaled(Feet, GameMath_FromVector3(Forward), DOG_CIRCLE_RADIUS);
        Npc->Id = Spawn(self, NpcNames[Index], Index, Feet, Npc->Heading, &Npc->BodyId);
        Spawned[Index] = Feet;
    }
    self->_areNpcsSpawned = true;
    Log(self, "the test NPCs: a human walking a square (crouching every third side) at (%.1f, %.1f, %.1f), a dog running "
        "circles and lying down at (%.1f, %.1f, %.1f), a pigeon walking, flying a loop and landing at (%.1f, %.1f, %.1f). "
        "F2 removes them.", Spawned[0].X, Spawned[0].Y, Spawned[0].Z, Spawned[1].X, Spawned[1].Y, Spawned[1].Z,
        Spawned[2].X, Spawned[2].Y, Spawned[2].Z);
}

/* F4: the player becomes the next kind of character (the same object and component, another asset); a human wears
 * the humanoid body, the others none. */
static void SwitchPlayerKind(WorldTestCharacters* self)
{
    size_t Previous = self->_playerKind;
    self->_playerKind = (self->_playerKind + 1U) % TEST_KIND_COUNT;
    WorldCharacterComponent* Character = FindCharacter(self->_world, self->_playerId);
    if (Character != NULL)
    {
        Check(self, WorldCharacterComponent_SetCharacter(Character, (const unsigned char*)KindAssets[self->_playerKind]),
            "changing the player's kind");
        if (Previous == TEST_KIND_HUMAN)
        {
            RemoveHumanBody(self, self->_playerId);
        }
        else if (self->_playerKind == TEST_KIND_HUMAN)
        {
            AddHumanBody(self, self->_playerId);
        }
    }
    Log(self, "you are a %s now (%s).", KindNames[self->_playerKind], KindAssets[self->_playerKind]);
}

/* F3: flight on and off; the player may fly whatever its kind (an override of can_fly). */
static void TogglePlayerFlight(WorldTestCharacters* self)
{
    self->_controller.WantsToFly = !self->_controller.WantsToFly;
    WorldCharacterComponent* Character = FindCharacter(self->_world, self->_playerId);
    if (Character != NULL)
    {
        PropertyValue CanFly = PropertyValue_FromBool(true);
        Check(self, WorldCharacterComponent_OverrideSetting(Character, CharacterSetting_CanFly, &CanFly),
            "letting the player fly");
    }
    Log(self, self->_controller.WantsToFly ? "flying: WASD along the view, SPACE up, CTRL down; F3 lands you."
        : "not flying: you fall to the ground.");
}


// Static functions: actions.
/* The first object named @p name, or 0. */
static uint64_t FindNamedObject(World* world, const unsigned char* name)
{
    size_t Count = World_GetObjectCount(world);
    for (size_t Index = 0U; Index < Count; Index++)
    {
        WorldObject* Object = NULL;
        Error Result = World_GetObjectByIndex(world, Index, &Object);
        Error_Deconstruct(&Result);
        const unsigned char* Candidate = (Object != NULL) ? WorldObject_GetName(Object) : NULL;
        if ((Candidate != NULL) && (strcmp((const char*)Candidate, (const char*)name) == 0))
        {
            return WorldObject_GetId(Object);
        }
    }
    return 0U;
}

/* What hangs on a holder's hand socket, or 0. */
static uint64_t FindHeld(WorldTestCharacters* self, uint64_t holderId)
{
    WorldObject* Holder = NULL;
    if ((holderId == 0U) || !World_TryGetObject(self->_world, holderId, &Holder))
    {
        return 0U;
    }
    for (const WorldObject* Child = WorldObject_GetFirstChild(Holder); Child != NULL;
        Child = WorldObject_GetNextSibling(Child))
    {
        const unsigned char* Socket = WorldObject_GetParentSocket(Child);
        if ((Socket != NULL) && (strcmp((const char*)Socket, HAND_SOCKET) == 0))
        {
            return WorldObject_GetId(Child);
        }
    }
    return 0U;
}

/* The action an object offers (its tbp:action text, or its nearest ancestor's) and the object that offers it; NULL for
 * none. */
static const unsigned char* FindOffer(WorldTestCharacters* self, uint64_t objectId, uint64_t* outOfferingId)
{
    uint32_t Key = 0U;
    WorldObject* Object = NULL;
    if (!KeyRegistry_TryFind(World_GetKeys(self->_world), ACTION_DATA_KEY, &Key)
        || !World_TryGetObject(self->_world, objectId, &Object))
    {
        return NULL;
    }
    while (Object != NULL)
    {
        const DataValue* Offer = DataMap_Find(WorldObject_GetData(Object), Key);
        if ((Offer != NULL) && (Offer->Type == DataType_String))
        {
            *outOfferingId = WorldObject_GetId(Object);
            return Offer->StringValue;
        }
        uint64_t ParentId = WorldObject_GetParentId(Object);
        Object = NULL;
        if ((ParentId != 0U) && !World_TryGetObject(self->_world, ParentId, &Object))
        {
            Object = NULL;
        }
    }
    return NULL;
}

/* Starts an action on a character's body, logging a refusal (out of reach, no such socket, no body). */
static bool StartAction(WorldTestCharacters* self, uint64_t characterId, const char* who, const unsigned char* action,
    uint64_t targetId)
{
    if (self->_animation == NULL)
    {
        return false;
    }
    Error Result = WorldAnimation_StartAction(self->_animation, characterId, action, targetId);
    if (Result.Code == ErrorCode_Success)
    {
        return true;
    }
    Log(self, "%s cannot \"%s\": %s", who, (const char*)action, (Result.Message != NULL) ? (const char*)Result.Message
        : "no details");
    Error_Deconstruct(&Result);
    return false;
}

/* The player used an object (E): in an action, it ends it (standing up from the bench); otherwise it starts the action
 * the object offers, unless its hand is full and the object would go in it. */
static void UseObject(WorldTestCharacters* self, uint64_t objectId)
{
    if ((self->_animation == NULL) || (self->_playerKind != TEST_KIND_HUMAN))
    {
        return;
    }
    if (WorldAnimation_EndAction(self->_animation, self->_playerId))
    {
        Log(self, "you end what you are doing.");
        return;
    }
    uint64_t Offering = 0U;
    const unsigned char* Action = (objectId != 0U) ? FindOffer(self, objectId, &Offering) : NULL;
    if (Action == NULL)
    {
        return;
    }
    if ((strcmp((const char*)Action, "pick_up") == 0) && (FindHeld(self, self->_playerId) != 0U))
    {
        Log(self, "your hand is full: 9 puts down what you hold.");
        return;
    }
    StartAction(self, self->_playerId, "you", Action, Offering);
}

/* 8: the NPC human heads for the bench to sit, or gets up from it (or stops heading there). */
static void ToggleNpcSitting(WorldTestCharacters* self)
{
    WorldTestNpc* Human = &self->_npcs[TEST_KIND_HUMAN];
    if (!self->_areNpcsSpawned || (Human->Id == 0U) || (self->_animation == NULL))
    {
        Log(self, "8 needs the test NPCs out (F2).");
        return;
    }
    if (Human->WantsToSit)
    {
        Human->WantsToSit = false;
        WorldAnimation_EndAction(self->_animation, Human->Id);
        Log(self, "the NPC human gets up (or stops heading for the bench).");
        return;
    }
    if (FindNamedObject(self->_world, BENCH_OBJECT_NAME) == 0U)
    {
        Log(self, "the test world has no bench.");
        return;
    }
    Human->WantsToSit = true;
    Log(self, "the NPC human heads for the bench to sit (8 gets it up).");
}

/* 9: the player puts down what it holds. */
static void PutDown(WorldTestCharacters* self)
{
    if ((self->_playerKind != TEST_KIND_HUMAN) || (FindHeld(self, self->_playerId) == 0U))
    {
        Log(self, "you hold nothing to put down.");
        return;
    }
    StartAction(self, self->_playerId, "you", (const unsigned char*)u8"put_down", 0U);
}

/* 0: the player (a human) and the NPC human wave, over whatever they do. */
static void WaveHello(WorldTestCharacters* self)
{
    if (self->_isWalking && (self->_playerKind == TEST_KIND_HUMAN))
    {
        StartAction(self, self->_playerId, "you", (const unsigned char*)u8"wave", 0U);
    }
    if (self->_areNpcsSpawned && (self->_npcs[TEST_KIND_HUMAN].Id != 0U))
    {
        StartAction(self, self->_npcs[TEST_KIND_HUMAN].Id, "the NPC human", (const unsigned char*)u8"wave", 0U);
    }
}

/* The NPC human heading for the bench (its brain while WantsToSit): it walks to a point in front of the bench, then
 * starts the sit action, which slides it onto the seat; seated it does nothing. Returns false when it does not want
 * to sit (its usual pattern drives it). */
static bool DriveToBench(WorldTestCharacters* self, WorldTestNpc* npc, Vector3Double feet, CharacterInput* input)
{
    if (!npc->WantsToSit || (self->_animation == NULL))
    {
        return false;
    }
    if (WorldAnimation_TryGetAction(self->_animation, npc->Id, NULL, NULL))
    {
        return true;
    }
    uint64_t BenchId = FindNamedObject(self->_world, BENCH_OBJECT_NAME);
    WorldObject* Bench = NULL;
    if ((BenchId == 0U) || !World_TryGetObject(self->_world, BenchId, &Bench))
    {
        npc->WantsToSit = false;
        return false;
    }
    // In front of the bench: its own +Z, turned by its yaw (radians, as objects keep their rotations).
    float Yaw = WorldObject_GetRotation(Bench).y;
    Vector3Double Front = Vector3Double_AddScaled(WorldObject_GetWorldPosition(Bench),
        Vector3Double_Create((double)sinf(Yaw), 0.0, (double)cosf(Yaw)), BENCH_APPROACH_DISTANCE);
    Vector3 Toward = GameMath_ToVector3Relative(Front, feet);
    Toward.y = 0.0f;
    float Distance = Vector3Length(Toward);
    if (Distance <= BENCH_START_DISTANCE)
    {
        if (!StartAction(self, npc->Id, "the NPC human", (const unsigned char*)u8"sit", BenchId))
        {
            npc->WantsToSit = false;
        }
        return true;
    }
    input->MoveDirection = Vector3Scale(Toward, 1.0f / Distance);
    input->LookYaw = atan2f(Toward.x, Toward.z);
    return true;
}

/* Remembers what a holder took, and forgets it (returning it) when it lets go. */
static void RememberHeld(WorldTestCharacters* self, uint64_t holderId, uint64_t heldId)
{
    for (size_t Index = 0U; Index < WORLD_TEST_MAX_HOLDERS; Index++)
    {
        if ((self->_held[Index].HolderId == holderId) || (self->_held[Index].HolderId == 0U))
        {
            self->_held[Index] = (WorldTestHeld){ .HolderId = holderId, .HeldId = heldId };
            return;
        }
    }
}

static uint64_t TakeHeld(WorldTestCharacters* self, uint64_t holderId)
{
    for (size_t Index = 0U; Index < WORLD_TEST_MAX_HOLDERS; Index++)
    {
        if (self->_held[Index].HolderId == holderId)
        {
            uint64_t Held = self->_held[Index].HeldId;
            self->_held[Index] = (WorldTestHeld){ .HolderId = 0U, .HeldId = 0U };
            return Held;
        }
    }
    return 0U;
}

/* An object let go of rests on what is under it, upright (a stand-in for physics: the test world's props do not fall). */
static void Settle(WorldTestCharacters* self, uint64_t id)
{
    WorldObject* Object = NULL;
    if ((id == 0U) || !World_TryGetObject(self->_world, id, &Object) || (WorldObject_GetParentId(Object) != 0U))
    {
        return;
    }
    uint32_t Mask = COLLISION_LAYER_BIT(CollisionLayer_World) | COLLISION_LAYER_BIT(CollisionLayer_Prop)
        | COLLISION_LAYER_BIT(CollisionLayer_Vehicle) | COLLISION_LAYER_BIT(CollisionLayer_Dynamic);
    WorldCollisionFilter Filter = WorldCollisionFilter_Create(Mask, WorldCollisionTriggerMode_Ignore);
    Filter.IgnoredObjectId = id;
    Filter.IsIgnoredSubtree = true;
    WorldRayHit Hit;
    Vector3Double From = WorldObject_GetWorldPosition(Object);
    if (!WorldCollision_Raycast(self->_collision, Vector3Double_Create(From.X, From.Y + 0.05, From.Z),
        (Vector3){ 0.0f, -1.0f, 0.0f }, SETTLE_SEARCH_DISTANCE, &Filter, &Hit) || Hit.IsStartInside)
    {
        return;
    }
    Check(self, WorldObject_SetPosition(Object, Vector3Double_Create(From.X, Hit.Point.Y, From.Z)), "settling an object");
    Check(self, WorldObject_SetRotation(Object, (Vector3){ 0.0f, WorldObject_GetRotation(Object).y, 0.0f }),
        "settling an object");
    WorldCollision_Teleport(self->_collision, id);
    Log(self, "object %" PRIu64 " (\"%s\") is set down at (%.2f, %.2f, %.2f).", id, GetName(self->_world, id), From.X,
        Hit.Point.Y, From.Z);
}

/* Puts down what a holder holds, where it is, and sets it on what is under it; forgets the holder's grab. */
static void DropHeld(WorldTestCharacters* self, uint64_t holderId)
{
    uint64_t Held = FindHeld(self, holderId);
    TakeHeld(self, holderId);
    if ((Held != 0U) && Check(self, World_SetParent(self->_world, Held, 0U, true), "putting down what was held"))
    {
        Settle(self, Held);
    }
}

/* Who an event is about, for the log: "you", the object's name, or its id once it is gone. */
static const char* GetWho(WorldTestCharacters* self, uint64_t id, char* buffer, size_t capacity)
{
    if (id == self->_playerId)
    {
        return "you";
    }
    const char* Name = GetName(self->_world, id);
    if (Name[0] != '\0')
    {
        return Name;
    }
    snprintf(buffer, capacity, "object %" PRIu64, id);
    return buffer;
}


// Static functions: the NPCs' brains.
/* The trunk of an NPC's body follows its crouch: squashed from the top, its feet in place. */
static void FitBody(WorldTestCharacters* self, const WorldTestNpc* npc, size_t kind, const WorldCharacterState* state)
{
    WorldObject* Body = NULL;
    if ((npc->BodyId == 0U) || !World_TryGetObject(self->_world, npc->BodyId, &Body))
    {
        return;
    }
    const TestBody* Shape = &Bodies[kind];
    float Squash = 1.0f - (0.35f * state->CrouchAmount);
    float Height = Shape->TrunkSize.y * Squash;
    float Bottom = Shape->TrunkCenter.y - (0.5f * Shape->TrunkSize.y);
    Vector3 Position = { Shape->TrunkCenter.x, Bottom + (0.5f * Height), Shape->TrunkCenter.z };
    Vector3 Scale = Vector3Scale((Vector3){ Shape->TrunkSize.x, Height, Shape->TrunkSize.z }, 1.0f / TEST_CUBE_SIZE);
    if (!Vector3Equals(WorldObject_GetScale(Body), Scale))
    {
        Check(self, WorldObject_SetScale(Body, Scale), "squashing a body");
        Check(self, WorldObject_SetPosition(Body, GameMath_FromVector3(Position)), "placing a body");
    }
}

/* A human walking a square: a leg of HUMAN_LEG_SECONDS, then a quarter turn; every HUMAN_CROUCH_EVERY-th leg crouched. */
static void DriveHuman(WorldTestNpc* npc, float deltaSeconds, CharacterInput* input)
{
    npc->Seconds += deltaSeconds;
    if (npc->Seconds >= HUMAN_LEG_SECONDS)
    {
        npc->Seconds = 0.0f;
        npc->Phase++;
        npc->Heading += 0.5f * PI;
    }
    input->MoveDirection = (Vector3){ sinf(npc->Heading), 0.0f, cosf(npc->Heading) };
    input->WantsCrouch = (npc->Phase % HUMAN_CROUCH_EVERY) == (HUMAN_CROUCH_EVERY - 1U);
    input->LookYaw = npc->Heading;
}

/* A dog running around a point, then lying down for a while. */
static void DriveDog(WorldTestNpc* npc, Vector3Double feet, float deltaSeconds, CharacterInput* input)
{
    npc->Seconds += deltaSeconds;
    bool IsRunning = (npc->Phase % 2U) == 0U;
    if (npc->Seconds >= (IsRunning ? DOG_RUN_SECONDS : DOG_REST_SECONDS))
    {
        npc->Seconds = 0.0f;
        npc->Phase++;
        IsRunning = !IsRunning;
    }
    input->WantsCrouch = !IsRunning;
    input->IsRunning = IsRunning;
    if (!IsRunning)
    {
        return;
    }
    // Chase a point running around the circle ahead of it (both measured from the circle's centre).
    Vector3 Offset = GameMath_ToVector3Relative(feet, npc->Center);
    float Angle = atan2f(Offset.x, Offset.z) + 0.6f;
    Vector3 Target = { DOG_CIRCLE_RADIUS * sinf(Angle), 0.0f, DOG_CIRCLE_RADIUS * cosf(Angle) };
    Vector3 Toward = Vector3Subtract(Target, Offset);
    Toward.y = 0.0f;
    float Length = Vector3Length(Toward);
    input->MoveDirection = (Length > 1e-4f) ? Vector3Scale(Toward, 1.0f / Length) : (Vector3){ 0.0f, 0.0f, 0.0f };
    input->LookYaw = atan2f(Toward.x, Toward.z);
}

/* A pigeon: walks, takes off and climbs, circles, glides down until it touches the ground, lands, walks again. */
static void DrivePigeon(WorldTestNpc* npc, Vector3Double feet, const WorldCharacterState* state, float deltaSeconds,
    CharacterInput* input)
{
    npc->Seconds += deltaSeconds;
    Vector3 Forward = { sinf(npc->Heading), 0.0f, cosf(npc->Heading) };
    switch ((PigeonPhase)npc->Phase)
    {
        case PigeonPhase_Walk:
            input->MoveDirection = Forward;
            input->WantsToFly = false;
            if (npc->Seconds >= PIGEON_WALK_SECONDS)
            {
                npc->Seconds = 0.0f;
                npc->Phase = PigeonPhase_Climb;
            }
            break;
        case PigeonPhase_Climb:
            input->MoveDirection = Vector3Normalize((Vector3){ Forward.x, 1.5f, Forward.z });
            input->WantsToFly = true;
            if (npc->Seconds >= PIGEON_CLIMB_SECONDS)
            {
                npc->Seconds = 0.0f;
                npc->Phase = PigeonPhase_Circle;
                npc->Center = Vector3Double_Add(feet, GameMath_FromVector3((Vector3){ Forward.z * PIGEON_CIRCLE_RADIUS,
                    0.0f, -Forward.x * PIGEON_CIRCLE_RADIUS }));
            }
            break;
        case PigeonPhase_Circle:
        {
            Vector3 Offset = GameMath_ToVector3Relative(feet, npc->Center);
            Vector3 Tangent = Vector3Normalize((Vector3){ -Offset.z, 0.0f, Offset.x });
            input->MoveDirection = Tangent;
            input->IsRunning = true;
            input->WantsToFly = true;
            if (npc->Seconds >= PIGEON_CIRCLE_SECONDS)
            {
                npc->Seconds = 0.0f;
                npc->Phase = PigeonPhase_Descend;
            }
            break;
        }
        case PigeonPhase_Descend:
            input->MoveDirection = Vector3Normalize((Vector3){ Forward.x * 0.4f, -1.0f, Forward.z * 0.4f });
            input->IsRunning = false;
            input->WantsToFly = !state->IsTouchingGround;
            if (state->IsTouchingGround)
            {
                npc->Seconds = 0.0f;
                npc->Phase = PigeonPhase_Walk;
                npc->Heading += 2.5f;
            }
            break;
    }
    float Horizontal = sqrtf((input->MoveDirection.x * input->MoveDirection.x)
        + (input->MoveDirection.z * input->MoveDirection.z));
    if (Horizontal > 1e-4f)
    {
        input->LookYaw = atan2f(input->MoveDirection.x, input->MoveDirection.z);
    }
}


// Static functions: events.
static void LogEvents(WorldTestCharacters* self)
{
    size_t Count = 0U;
    const WorldCharacterEvent* Events = WorldCharacters_GetEvents(self->_characters, &Count);
    for (size_t Index = 0U; Index < Count; Index++)
    {
        const WorldCharacterEvent* Event = &Events[Index];
        const char* Who = (Event->CharacterId == self->_playerId) ? "you" : GetName(self->_world, Event->CharacterId);
        switch (Event->Kind)
        {
            case WorldCharacterEventKind_Jumped:
                Log(self, "%s jumped.", Who);
                break;
            case WorldCharacterEventKind_Landed:
                Log(self, "%s landed on object %" PRIu64 " (\"%s\") at %.2f m/s.", Who, Event->OtherId,
                    GetName(self->_world, Event->OtherId), (double)Event->Value);
                break;
            case WorldCharacterEventKind_LeftGround:
                Log(self, "%s left the ground.", Who);
                break;
            case WorldCharacterEventKind_Crouched:
                Log(self, "%s crouched.", Who);
                break;
            case WorldCharacterEventKind_StoodUp:
                Log(self, "%s stood up.", Who);
                break;
            case WorldCharacterEventKind_TookOff:
                Log(self, "%s took off.", Who);
                break;
            case WorldCharacterEventKind_StoppedFlying:
                Log(self, "%s stopped flying.", Who);
                break;
            case WorldCharacterEventKind_Used:
                if (Event->OtherId != 0U)
                {
                    Log(self, "%s used object %" PRIu64 " (\"%s\"), %.2f m away.", Who, Event->OtherId,
                        GetName(self->_world, Event->OtherId), (double)Event->Value);
                }
                else
                {
                    Log(self, "%s used nothing: nothing within reach.", Who);
                }
                if (Event->CharacterId == self->_playerId)
                {
                    UseObject(self, Event->OtherId);
                }
                break;
        }
    }
    WorldCharacters_ClearEvents(self->_characters);
}


// Functions (WorldTestCharacters.h).
void WorldTestCharacters_Construct(WorldTestCharacters* self, World* world, WorldCollision* collision,
    WorldCharacters* characters, GameInput* input, Logger* logger)
{
    Memory_Zero(self, sizeof(*self));
    self->_world = world;
    self->_collision = collision;
    self->_characters = characters;
    self->_logger = logger;
    PlayerController_Construct(&self->_controller);
    self->_playerKind = TEST_KIND_HUMAN;
    self->_hasWaveAction = (input != NULL) && GameInput_FindActionText(input, WORLD_TEST_WAVE_ACTION, &self->_waveAction);
    if (!self->_hasWaveAction)
    {
        Log(self, "the wave action \"%s\" is not registered (input_actions/test_frame.json): nobody waves.",
            (const char*)WORLD_TEST_WAVE_ACTION);
    }
}

void WorldTestCharacters_Deconstruct(WorldTestCharacters* self)
{
    PlayerController_Deconstruct(&self->_controller);
}

void WorldTestCharacters_SetAnimation(WorldTestCharacters* self, WorldAnimation* animation)
{
    self->_animation = animation;
}

uint64_t WorldTestCharacters_HandleAnimationEvents(WorldTestCharacters* self)
{
    if (self->_animation == NULL)
    {
        return 0U;
    }
    uint64_t Pushed = 0U;
    size_t Count = 0U;
    const WorldAnimationEvent* Events = WorldAnimation_GetEvents(self->_animation, &Count);
    for (size_t Index = 0U; Index < Count; Index++)
    {
        const WorldAnimationEvent* Event = &Events[Index];
        char WhoText[32];
        const char* Who = GetWho(self, Event->ObjectId, WhoText, sizeof(WhoText));
        const char* Name = (const char*)Event->Name;
        switch (Event->Kind)
        {
            case WorldAnimationEventKind_ActionStarted:
                Log(self, "%s: action \"%s\" starts (target %" PRIu64 " \"%s\").", Who, Name, Event->TargetId,
                    GetName(self->_world, Event->TargetId));
                break;
            case WorldAnimationEventKind_ActionEnded:
                Log(self, "%s: action \"%s\" ends.", Who, Name);
                break;
            case WorldAnimationEventKind_ActionFailed:
                Log(self, "%s: action \"%s\" failed (its approach did not arrive).", Who, Name);
                break;
            case WorldAnimationEventKind_Graph:
                Log(self, "%s: \"%s\".", Who, Name);
                if ((strcmp(Name, "grab") == 0) && (Event->TargetId != 0U)
                    && (FindHeld(self, Event->ObjectId) == Event->TargetId))
                {
                    RememberHeld(self, Event->ObjectId, Event->TargetId);
                }
                else if (strcmp(Name, "release") == 0)
                {
                    Settle(self, TakeHeld(self, Event->ObjectId));
                }
                else if (strcmp(Name, "push") == 0)
                {
                    Pushed = Event->TargetId;
                }
                break;
        }
    }
    WorldAnimation_ClearEvents(self->_animation);
    return Pushed;
}

void WorldTestCharacters_HandleInput(WorldTestCharacters* self, GameInput* input, GameCamera* camera,
    bool isCursorCaptured)
{
    if (GameInput_IsKeyPressed(input, GameKey_F1))
    {
        if (self->_isWalking)
        {
            StopWalking(self);
        }
        else
        {
            StartWalking(self, camera);
        }
    }
    if (GameInput_IsKeyPressed(input, GameKey_F2))
    {
        ToggleNpcs(self, camera);
    }
    if (GameInput_IsKeyPressed(input, GameKey_8))
    {
        ToggleNpcSitting(self);
    }
    if (self->_hasWaveAction && GameInput_IsActionPressed(input, self->_waveAction))
    {
        WaveHello(self);
    }
    if (!self->_isWalking)
    {
        return;
    }
    if (GameInput_IsKeyPressed(input, GameKey_F3))
    {
        TogglePlayerFlight(self);
    }
    if (GameInput_IsKeyPressed(input, GameKey_F4))
    {
        SwitchPlayerKind(self);
    }
    if (GameInput_IsKeyPressed(input, GameKey_9))
    {
        PutDown(self);
    }
    PlayerController_Update(&self->_controller, input, self->_world, isCursorCaptured);
}

void WorldTestCharacters_UpdateBrains(WorldTestCharacters* self, float deltaSeconds)
{
    for (size_t Index = 0U; Index < WORLD_TEST_NPC_COUNT; Index++)
    {
        WorldTestNpc* Npc = &self->_npcs[Index];
        WorldCharacterComponent* Character = FindCharacter(self->_world, Npc->Id);
        if (Character == NULL)
        {
            continue;
        }
        const WorldCharacterState* State = WorldCharacterComponent_GetState(Character);
        Vector3Double Feet = WorldObject_GetWorldPosition(WorldComponent_GetOwner(
            WorldCharacterComponent_AsComponent(Character)));
        CharacterInput Input = CharacterInput_CreateDefault();
        Input.Facing = CharacterFacing_Movement;
        switch (Index)
        {
            case TEST_KIND_HUMAN:
                if (!DriveToBench(self, Npc, Feet, &Input))
                {
                    DriveHuman(Npc, deltaSeconds, &Input);
                }
                break;
            case TEST_KIND_DOG:
                DriveDog(Npc, Feet, deltaSeconds, &Input);
                break;
            default:
                DrivePigeon(Npc, Feet, State, deltaSeconds, &Input);
                break;
        }
        Check(self, WorldCharacterComponent_SetInput(Character, &Input), "driving an NPC");
        FitBody(self, Npc, Index, State);
    }
}

void WorldTestCharacters_AfterStep(WorldTestCharacters* self)
{
    LogEvents(self);
    if (!self->_isWalking)
    {
        return;
    }
    if (FindCharacter(self->_world, self->_playerId) == NULL)
    {
        // Something removed the player (a reload): back to the flycam.
        TakeHeld(self, self->_playerId);
        self->_playerId = 0U;
        StopWalking(self);
        return;
    }
    if (PlayerController_UpdateLookTarget(&self->_controller, self->_characters))
    {
        WorldRayHit Hit;
        if (PlayerController_GetLookTarget(&self->_controller, &Hit))
        {
            Log(self, "you look at object %" PRIu64 " (\"%s\"), %.2f m away.", Hit.Shape.ObjectId,
                GetName(self->_world, Hit.Shape.ObjectId), (double)Hit.Distance);
        }
        else
        {
            Log(self, "you look at nothing within reach.");
        }
    }
}

void WorldTestCharacters_PlaceCamera(const WorldTestCharacters* self, GameCamera* camera)
{
    if (self->_isWalking)
    {
        PlayerController_ApplyCamera(&self->_controller, self->_world, camera);
    }
}

size_t WorldTestCharacters_Shift(WorldTestCharacters* self, Vector3Double offset)
{
    // Every character, not only the test ones: one the world file placed moved with its object too.
    size_t Count = 0U;
    size_t CharacterCount = 0U;
    WorldComponent* const* Characters = World_GetComponents(self->_world, WorldComponentType_Character,
        &CharacterCount);
    for (size_t Index = 0U; Index < CharacterCount; Index++)
    {
        const WorldCharacterComponent* Character = WorldCharacterComponent_FromComponent(Characters[Index]);
        const WorldObject* Object = WorldComponent_GetOwner(Characters[Index]);
        uint64_t Id = WorldObject_GetId(Object);
        // The player keeps where it looks (its view must not jump); the others keep facing where they faced.
        float Yaw = (self->_isWalking && (Id == self->_playerId)) ? WorldCharacterComponent_GetInput(Character)->LookYaw
            : WorldCharacterComponent_GetState(Character)->FacingYaw;
        if (Check(self, WorldCharacters_Teleport(self->_characters, Id, WorldObject_GetWorldPosition(Object), Yaw),
            "placing a character again"))
        {
            Count++;
        }
    }
    for (size_t Index = 0U; Index < WORLD_TEST_NPC_COUNT; Index++)
    {
        WorldTestNpc* Npc = &self->_npcs[Index];
        if (Npc->Id != 0U)
        {
            Npc->Center = Vector3Double_Add(Npc->Center, offset);
        }
    }
    return Count;
}

void WorldTestCharacters_LogStatus(WorldTestCharacters* self)
{
    WorldCharacterComponent* Character = FindCharacter(self->_world, self->_playerId);
    if (!self->_isWalking || (Character == NULL))
    {
        Log(self, "walk mode is off (F1 walks); %s.", self->_areNpcsSpawned ? "the test NPCs are out (F2)"
            : "no test NPCs (F2)");
        return;
    }
    const WorldCharacterState* State = WorldCharacterComponent_GetState(Character);
    CharacterSettings Settings = WorldCharacterComponent_GetSettings(Character);
    float Speed = sqrtf((State->Velocity.x * State->Velocity.x) + (State->Velocity.z * State->Velocity.z));
    WorldRayHit Hit;
    bool IsLooking = PlayerController_GetLookTarget(&self->_controller, &Hit);
    char Ground[160];
    if (State->IsGrounded)
    {
        snprintf(Ground, sizeof(Ground), "on object %" PRIu64 " (\"%s\")", State->GroundObjectId,
            GetName(self->_world, State->GroundObjectId));
    }
    else
    {
        snprintf(Ground, sizeof(Ground), "in the air for %.2f s", (double)State->TimeInAir);
    }
    char Looking[160];
    if (IsLooking)
    {
        snprintf(Looking, sizeof(Looking), "looking at object %" PRIu64 " (\"%s\") %.2f m away", Hit.Shape.ObjectId,
            GetName(self->_world, Hit.Shape.ObjectId), (double)Hit.Distance);
    }
    else
    {
        snprintf(Looking, sizeof(Looking), "looking at nothing within %.1f m", (double)Settings.InteractionReach);
    }
    // What the body does: its action (and on what) and what its hand holds.
    char Doing[200];
    const unsigned char* Action = NULL;
    uint64_t ActionTarget = 0U;
    uint64_t Held = FindHeld(self, self->_playerId);
    int DoingLength = ((self->_animation != NULL)
        && WorldAnimation_TryGetAction(self->_animation, self->_playerId, &Action, &ActionTarget))
        ? snprintf(Doing, sizeof(Doing), "in action \"%s\" (target %" PRIu64 " \"%s\")", (const char*)Action,
            ActionTarget, GetName(self->_world, ActionTarget))
        : snprintf(Doing, sizeof(Doing), "in no action");
    if ((Held != 0U) && (DoingLength > 0) && ((size_t)DoingLength < sizeof(Doing)))
    {
        snprintf(Doing + DoingLength, sizeof(Doing) - (size_t)DoingLength, ", holding object %" PRIu64 " (\"%s\")", Held,
            GetName(self->_world, Held));
    }
    Log(self, "you: a %s (%s), %s, %s%s, %s, %.2f m/s, eyes %.2f m up; %s; %s; %s.",
        KindNames[self->_playerKind], KindAssets[self->_playerKind],
        (State->Mode == CharacterMovementMode_Flying) ? "flying" : "walking",
        (State->Stance == CharacterStance_Crouching) ? "crouching" : "standing",
        State->IsStandUpBlocked ? " (no room to stand)" : "", Ground, (double)Speed, (double)State->EyeHeight, Looking,
        Doing, self->_areNpcsSpawned ? "the test NPCs are out" : "no test NPCs");
}
