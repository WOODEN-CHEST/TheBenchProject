// The world test frame's animation (T29, WorldTestAnimation.h): the animation system the frame ticks and its debug keys:
// the showcase figure's clips, a crowd of placeholder figures and slow motion.

#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include "WorldTestAnimation.h"
#include "WorldAnimatorComponent.h"
#include "WorldModelComponent.h"
#include "WorldObject.h"
#include "wr/WRString.h"


// Macros.
/** The test world's showcase figure, which the 5 key steps through the clips. */
#define SHOWCASE_OBJECT_NAME ((const unsigned char*)u8"humanoid_showcase")
/** The crowd's world models, alternating: the tall figure and the short one. */
#define CROWD_TALL_WORLD_MODEL ((const unsigned char*)u8"test_humanoid")
#define CROWD_SHORT_WORLD_MODEL ((const unsigned char*)u8"test_humanoid_short")
/** The clip the crowd walks with. */
#define CROWD_CLIP ((const unsigned char*)u8"walk")
/** Metres between the crowd's figures, and how far in front of the camera its near row stands. */
#define CROWD_SPACING 1.3
#define CROWD_DISTANCE 4.0
/** The height the crowd stands at: the test world's ground's top. */
#define CROWD_GROUND_HEIGHT 0.05
/** The step between two figures' phases: the golden ratio's share of a stride, so no two walk in step. */
#define CROWD_PHASE_STEP 0.6180339887
/** The spread of the crowd's speeds around 1 (each figure a little faster or slower). */
#define CROWD_SPEED_SPREAD 0.15f
/** Room for one log line's text and for a crowd figure's name. */
#define LOG_TEXT_CAPACITY 512
#define CROWD_NAME_CAPACITY 32


// Fields.
/* The placeholder library's clips, in the order the 5 key plays them. */
static const char* const ShowcaseClips[] =
{
    "idle", "walk", "run", "crouch_idle", "crouch_walk", "jump", "fall", "land", "wave", "sit_down", "sit_idle", "stand_up",
    "pick_up", "hold_r", "put_down", "open_door"
};


// Static functions.
static void Log(const WorldTestAnimation* self, const char* format, ...)
{
    if (self->_logger == NULL)
    {
        return;
    }
    char Text[LOG_TEXT_CAPACITY];
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

/* The id of the world's first object named @p name, or 0. */
static uint64_t FindObjectId(World* world, const unsigned char* name)
{
    size_t Count = World_GetObjectCount(world);
    for (size_t Index = 0U; Index < Count; Index++)
    {
        WorldObject* Object = NULL;
        bool IsFound = false;
        Error Result = World_GetObjectByIndex(world, Index, &Object);
        if ((Result.Code == ErrorCode_Success) && (WorldObject_GetName(Object) != NULL))
        {
            Result = StringUTF8_EqualsExact(WorldObject_GetName(Object), name, &IsFound);
        }
        Error_Deconstruct(&Result);
        if (IsFound)
        {
            return WorldObject_GetId(Object);
        }
    }
    return 0U;
}

/* An object's animator, or NULL. */
static WorldAnimatorComponent* FindAnimator(World* world, uint64_t id)
{
    WorldComponent* Component = NULL;
    return World_TryGetComponent(world, id, WorldComponentType_Animator, &Component)
        ? WorldAnimatorComponent_FromComponent(Component) : NULL;
}

/* Logs a failed step of the debug keys, and releases the error. */
static void LogFailure(const WorldTestAnimation* self, const char* what, Error* error)
{
    Log(self, "%s failed: %s", what, (error->Message != NULL) ? (const char*)error->Message : "no details");
    Error_Deconstruct(error);
}

/* The 5 key: the showcase's next clip, through its clip row (the animation system cross-fades to it). */
static void StepShowcase(WorldTestAnimation* self)
{
    WorldAnimatorComponent* Animator = FindAnimator(self->_world, self->_showcaseId);
    if (Animator == NULL)
    {
        Log(self, "the test world has no showcase figure (\"%s\" with an animator).", (const char*)SHOWCASE_OBJECT_NAME);
        return;
    }
    size_t Next = (self->_showcaseClip + 1U) % (sizeof(ShowcaseClips) / sizeof(ShowcaseClips[0]));
    Error Result = WorldAnimatorComponent_SetPlaysClip(Animator, true);
    if (Result.Code == ErrorCode_Success)
    {
        Result = WorldAnimatorComponent_SetClip(Animator, (const unsigned char*)ShowcaseClips[Next]);
    }
    if (Result.Code != ErrorCode_Success)
    {
        LogFailure(self, "the showcase's clip", &Result);
        return;
    }
    self->_showcaseClip = Next;
    Log(self, "the showcase figure cross-fades to \"%s\" over %.2f s (5 steps on).", ShowcaseClips[Next],
        (double)WorldAnimatorComponent_GetFadeSeconds(Animator));
}

/* Adds one crowd figure at @p position facing @p yaw (radians), walking at @p phase and @p speed. */
static Error AddCrowdFigure(WorldTestAnimation* self, size_t index, Vector3Double position, float yaw, float phase,
    float speed, uint64_t* outId)
{
    char Name[CROWD_NAME_CAPACITY];
    snprintf(Name, sizeof(Name), "crowd_%u", (unsigned int)index);
    WorldObject* Object = NULL;
    Error Result = World_CreateObject(self->_world, (const unsigned char*)Name, &Object);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    *outId = WorldObject_GetId(Object);
    WorldObject_SetPersistent(Object, false);
    Result = WorldObject_SetPosition(Object, position);
    if (Result.Code == ErrorCode_Success)
    {
        Result = WorldObject_SetRotation(Object, (Vector3){ 0.0f, yaw, 0.0f });
    }
    WorldComponent* Component = NULL;
    if (Result.Code == ErrorCode_Success)
    {
        Result = World_AddComponent(self->_world, *outId, WorldComponentType_Model, &Component);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = WorldModelComponent_SetWorldModel(WorldModelComponent_FromComponent(Component),
            ((index % 2U) == 0U) ? CROWD_TALL_WORLD_MODEL : CROWD_SHORT_WORLD_MODEL);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = World_AddComponent(self->_world, *outId, WorldComponentType_Animator, &Component);
    }
    WorldAnimatorComponent* Animator = WorldAnimatorComponent_FromComponent(Component);
    if (Result.Code == ErrorCode_Success)
    {
        Result = WorldAnimatorComponent_SetClip(Animator, CROWD_CLIP);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = WorldAnimatorComponent_SetPlaysClip(Animator, true);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = WorldAnimatorComponent_SetPhase(Animator, phase);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = WorldAnimatorComponent_SetSpeed(Animator, speed);
    }
    return Result;
}

/* Removes the crowd's objects (those still there). */
static void RemoveCrowd(WorldTestAnimation* self)
{
    for (size_t Index = 0U; Index < WORLD_TEST_CROWD_COUNT; Index++)
    {
        WorldObject* Object = NULL;
        if ((self->_crowdIds[Index] != 0U) && World_TryGetObject(self->_world, self->_crowdIds[Index], &Object))
        {
            Error Result = World_RemoveObject(self->_world, self->_crowdIds[Index]);
            Error_Deconstruct(&Result);
        }
        self->_crowdIds[Index] = 0U;
    }
    self->_isCrowdShown = false;
}

/* The 6 key's crowd: a square of figures in front of the camera on the ground, facing it, each at its own phase. */
static void AddCrowd(WorldTestAnimation* self, const GameCamera* camera)
{
    Vector3 Forward = GameCamera_GetForward(camera);
    Vector3 Right = GameCamera_GetRight(camera);
    double ForwardLength = sqrt(((double)Forward.x * (double)Forward.x) + ((double)Forward.z * (double)Forward.z));
    double ForwardX = (ForwardLength > 0.0) ? ((double)Forward.x / ForwardLength) : 0.0;
    double ForwardZ = (ForwardLength > 0.0) ? ((double)Forward.z / ForwardLength) : 1.0;
    double RightLength = sqrt(((double)Right.x * (double)Right.x) + ((double)Right.z * (double)Right.z));
    double RightX = (RightLength > 0.0) ? ((double)Right.x / RightLength) : -ForwardZ;
    double RightZ = (RightLength > 0.0) ? ((double)Right.z / RightLength) : ForwardX;
    // The figures face +Z: turned to face back along the camera's view.
    float Yaw = (float)atan2(-ForwardX, -ForwardZ);
    double HalfWidth = (CROWD_SPACING * (double)(WORLD_TEST_CROWD_SIDE - 1U)) * 0.5;
    Error Result = Error_CreateSuccess();
    for (size_t Index = 0U; (Index < WORLD_TEST_CROWD_COUNT) && (Result.Code == ErrorCode_Success); Index++)
    {
        double Row = CROWD_DISTANCE + (CROWD_SPACING * (double)(Index / WORLD_TEST_CROWD_SIDE));
        double Column = (CROWD_SPACING * (double)(Index % WORLD_TEST_CROWD_SIDE)) - HalfWidth;
        Vector3Double Position =
        {
            .X = camera->Position.X + (ForwardX * Row) + (RightX * Column), .Y = CROWD_GROUND_HEIGHT,
            .Z = camera->Position.Z + (ForwardZ * Row) + (RightZ * Column)
        };
        double Phase = fmod(CROWD_PHASE_STEP * (double)Index, 1.0);
        float Speed = 1.0f + (CROWD_SPEED_SPREAD * (float)((2.0 * fmod(Phase * 7.0, 1.0)) - 1.0));
        Result = AddCrowdFigure(self, Index, Position, Yaw, (float)Phase, Speed, &self->_crowdIds[Index]);
    }
    self->_isCrowdShown = true;
    if (Result.Code != ErrorCode_Success)
    {
        RemoveCrowd(self);
        LogFailure(self, "the crowd", &Result);
        return;
    }
    Log(self, "a crowd of %u figures walks %.0f m ahead, each at its own phase (6 removes it).",
        (unsigned int)WORLD_TEST_CROWD_COUNT, CROWD_DISTANCE);
}


// Functions (declared in WorldTestAnimation.h).
Error WorldTestAnimation_Construct(WorldTestAnimation* self, World* world, AssetManager* assets, Logger* logger)
{
    *self = (WorldTestAnimation){ ._world = world, ._logger = logger };
    self->_showcaseId = FindObjectId(world, SHOWCASE_OBJECT_NAME);
    return WorldAnimation_Create(world, assets, logger, &self->_animation);
}

void WorldTestAnimation_Deconstruct(WorldTestAnimation* self)
{
    WorldAnimation_Destroy(self->_animation);
    self->_animation = NULL;
}

void WorldTestAnimation_HandleInput(WorldTestAnimation* self, const GameInput* input, const GameCamera* camera)
{
    if (GameInput_IsKeyPressed(input, GameKey_5))
    {
        StepShowcase(self);
    }
    if (GameInput_IsKeyPressed(input, GameKey_6))
    {
        if (self->_isCrowdShown)
        {
            RemoveCrowd(self);
            Log(self, "the crowd is gone (6 brings it back).");
        }
        else
        {
            AddCrowd(self, camera);
        }
    }
    if (GameInput_IsKeyPressed(input, GameKey_7))
    {
        self->_isSlowMotion = !self->_isSlowMotion;
        Log(self, "slow motion %s (every animation at %.0f%% of its speed; 7 switches).",
            self->_isSlowMotion ? "on" : "off", self->_isSlowMotion ? (double)(WORLD_TEST_SLOW_MOTION_SCALE * 100.0f)
            : 100.0);
    }
}

void WorldTestAnimation_Update(WorldTestAnimation* self, float deltaSeconds)
{
    WorldAnimation_Update(self->_animation, self->_isSlowMotion ? (deltaSeconds * WORLD_TEST_SLOW_MOTION_SCALE)
        : deltaSeconds);
}

void WorldTestAnimation_Pose(WorldTestAnimation* self)
{
    WorldAnimation_Pose(self->_animation);
}

void WorldTestAnimation_LogStatus(const WorldTestAnimation* self)
{
    WorldAnimationStatus Status = WorldAnimation_GetStatus(self->_animation);
    const WorldAnimatorComponent* Showcase = FindAnimator(self->_world, self->_showcaseId);
    const unsigned char* Clip = (Showcase != NULL) ? WorldAnimatorComponent_GetClip(Showcase) : NULL;
    Log(self, "animation: %" PRIu64 " animators (%" PRIu64 " posed), %" PRIu64 " clips playing, updated in %.0f us; the "
        "last frame posed %" PRIu64 " (%" PRIu64 " bones) in %.0f us; the showcase plays \"%s\" (5); crowd %s (6); slow "
        "motion %s (7).", (uint64_t)Status.AnimatorCount, (uint64_t)Status.PosedCount, (uint64_t)Status.ClipCount,
        Status.UpdateMicroseconds, (uint64_t)Status.RecomputedCount, (uint64_t)Status.BoneCount, Status.PoseMicroseconds,
        (Clip != NULL) ? (const char*)Clip : "nothing", self->_isCrowdShown ? "out" : "away",
        self->_isSlowMotion ? "on" : "off");
}
