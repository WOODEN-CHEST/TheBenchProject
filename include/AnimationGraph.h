#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "wr/WRJSON.h"


/**
 * @file AnimationGraph.h
 * @brief Animation graphs (T30): which clip an animated object plays when, as data creators edit (the owner's decision
 *        G31), read from the "animation_graph" asset type (JSON in animation_graphs/; references/asset_structure.md,
 *        "Animation graphs").
 *
 * WHAT. A graph is a set of LAYERS, each a state machine. A layer's STATES play nothing (an empty state), one clip, or
 * a blend of clips by a parameter (idle, walk and run by the character's speed), and its TRANSITIONS move it from state
 * to state when their conditions hold, cross-fading over a time. The first layer is the body; every further layer
 * plays over the layers before it on the bones of its mask (the arms holding a cup while the legs walk), by its
 * weight. A graph names clips, not libraries: the animator plays it with the clips of its animation libraries
 * (AnimationGraphPlayer.h binds it), so one graph serves every model of a skeleton layout.
 *
 * PARAMETERS. Conditions, blends and speeds read parameters: the BUILT-IN ones, which the animation system fills every
 * tick from the object's character (AnimationGraphBuiltIn: its speed, ground, flight, crouch, time in the air, turn
 * rate, look; zero without a character), and the graph's CUSTOM ones (floats and booleans with a default, set by code,
 * scripts and the graph's own events). EVENTS are what is true for one tick: the character's (jumped, landed, ...,
 * AnimationGraphEvent), an action's end request, and the graph's custom TRIGGERS, which code fires.
 *
 * ACTIONS. Special actions (sitting on a bench, picking something up) are entry points into a layer's states: the
 * animation system starts one through its API, first sliding the character onto an anchor when the action has one,
 * locks the character's movement as the action says, and ends it when the layer leaves the action's states. States
 * fire timed EVENTS (by time or phase), which may attach the action's target to a socket, detach what hangs on a
 * socket, and set custom parameters; every event is also queued for the game (sounds, scripts).
 *
 * LOOK-AT. A graph may turn some bones (the neck, the head) toward where its character looks, within limits and at a
 * turn speed; each state says how much (its look-at weight).
 *
 * MEMORY. A graph is ONE block that holds only offsets and indices, never pointers, so it moves and copies as bytes
 * (AnimationGraph_Clone): the asset type parses a definition once when the definitions are read (every rule checked
 * then) and each load copies it. Read it through the accessors below. It is immutable once made.
 */


// Macros: limits (the reader refuses anything past them; downloaded content stays bounded).
/** @brief The most bytes a name in a graph may have (a layer, state, parameter, action or event): [a-z][a-z0-9_.-]*. */
#define ANIMATION_GRAPH_NAME_MAX_LENGTH ((size_t)64)
/** @brief The most custom parameters (floats, booleans and triggers together) a graph may declare. */
#define ANIMATION_GRAPH_MAX_PARAMETERS 64U
/** @brief The most custom triggers among them (with the built-in events they fit one 64-bit set). */
#define ANIMATION_GRAPH_MAX_TRIGGERS 32U
/** @brief The most layers a graph may have. */
#define ANIMATION_GRAPH_MAX_LAYERS 8U
/** @brief The most states a layer may have. */
#define ANIMATION_GRAPH_MAX_STATES 64U
/** @brief The most transitions a layer may have (a transition from a list of states counts once per state). */
#define ANIMATION_GRAPH_MAX_TRANSITIONS 256U
/** @brief The most conditions a transition may have. */
#define ANIMATION_GRAPH_MAX_CONDITIONS 8U
/** @brief The fewest and the most clips a blend may have. */
#define ANIMATION_GRAPH_MIN_BLEND_POINTS 2U
#define ANIMATION_GRAPH_MAX_BLEND_POINTS 8U
/** @brief The most events a state may fire. */
#define ANIMATION_GRAPH_MAX_EVENTS 16U
/** @brief The most parameters one event may set. */
#define ANIMATION_GRAPH_MAX_EVENT_SETS 4U
/** @brief The most actions a graph may have. */
#define ANIMATION_GRAPH_MAX_ACTIONS 64U
/** @brief The most states an action may have. */
#define ANIMATION_GRAPH_MAX_ACTION_STATES 16U
/** @brief The most bones a layer's mask may name (each with its subtree). */
#define ANIMATION_GRAPH_MAX_MASK_BONES 32U
/** @brief The most bones look-at may turn. */
#define ANIMATION_GRAPH_MAX_LOOK_BONES 8U
/** @brief The most bytes a bone's name may have: the first 31 bytes, as skeletons keep them. */
#define ANIMATION_GRAPH_BONE_NAME_MAX_LENGTH ((size_t)31)
/** @brief The most bytes a socket's name may have ([a-z][a-z0-9_.-]*). */
#define ANIMATION_GRAPH_SOCKET_NAME_MAX_LENGTH ((size_t)64)
/** @brief The longest fade, approach and event time, in seconds. */
#define ANIMATION_GRAPH_MAX_SECONDS 60.0f
/** @brief The fastest a state may play (its speed and its rate limits). */
#define ANIMATION_GRAPH_MAX_SPEED 100.0f
/** @brief The farthest an action's anchor may be when the action starts, in metres. */
#define ANIMATION_GRAPH_MAX_ANCHOR_DISTANCE 100.0f
/** @brief The largest magnitude a custom parameter's default, bounds or a condition's value may have. */
#define ANIMATION_GRAPH_MAX_VALUE 1.0e9f
/** @brief The fastest look-at may turn, in degrees per second. */
#define ANIMATION_GRAPH_MAX_LOOK_TURN_SPEED 100000.0f
/** @brief No index: a transition from any state, a state without a speed parameter, an event without an attach. */
#define ANIMATION_GRAPH_NONE UINT32_MAX

/** @brief The cross-fade of a transition or an action that sets none, unless the graph sets its own default. */
#define ANIMATION_GRAPH_DEFAULT_FADE_SECONDS 0.2f
/** @brief The rate limits of a state that matches a speed and sets none. */
#define ANIMATION_GRAPH_DEFAULT_MIN_RATE 0.25f
#define ANIMATION_GRAPH_DEFAULT_MAX_RATE 4.0f
/** @brief An anchor's reach and approach time when an action sets none. */
#define ANIMATION_GRAPH_DEFAULT_ANCHOR_DISTANCE 1.5f
#define ANIMATION_GRAPH_DEFAULT_APPROACH_SECONDS 0.5f
/** @brief Look-at's limits and turn speed when the graph sets none, in degrees and degrees per second. */
#define ANIMATION_GRAPH_DEFAULT_LOOK_MAX_YAW 70.0f
#define ANIMATION_GRAPH_DEFAULT_LOOK_MAX_PITCH_UP 40.0f
#define ANIMATION_GRAPH_DEFAULT_LOOK_MAX_PITCH_DOWN 50.0f
#define ANIMATION_GRAPH_DEFAULT_LOOK_TURN_SPEED 360.0f


// Types: the vocabulary.
/**
 * @brief The built-in parameters, in this order the first parameter indices of every graph: the animation system fills
 *        them every tick from the object's character (WorldCharacterComponent.h), zero without one. Speeds are metres per
 *        second relative to the ground under the character (what it really did, not what it wished), angles degrees,
 *        booleans 0 or 1. Their JSON names are in brackets.
 */
typedef enum AnimationGraphBuiltInEnum
{
    /** @brief ["speed"] How fast it moves across the ground (the velocity's horizontal length). */
    AnimationGraphBuiltIn_Speed = 0,
    /** @brief ["speed_3d"] How fast it moves at all (the velocity's length: flight). */
    AnimationGraphBuiltIn_Speed3d = 1,
    /** @brief ["forward_speed"] Its velocity along its facing (negative backwards). */
    AnimationGraphBuiltIn_ForwardSpeed = 2,
    /** @brief ["side_speed"] Its velocity to its right (negative to the left). */
    AnimationGraphBuiltIn_SideSpeed = 3,
    /** @brief ["vertical_speed"] Its velocity up (negative falling). */
    AnimationGraphBuiltIn_VerticalSpeed = 4,
    /** @brief ["is_grounded"] Whether it stands on walkable ground (walking). */
    AnimationGraphBuiltIn_IsGrounded = 5,
    /** @brief ["is_touching_ground"] Whether walkable ground is right below it (also while flying). */
    AnimationGraphBuiltIn_IsTouchingGround = 6,
    /** @brief ["is_flying"] Whether it flies. */
    AnimationGraphBuiltIn_IsFlying = 7,
    /** @brief ["is_crouching"] Whether it crouches. */
    AnimationGraphBuiltIn_IsCrouching = 8,
    /** @brief ["crouch_amount"] How far its eyes are down toward its crouched height, 0 to 1. */
    AnimationGraphBuiltIn_CrouchAmount = 9,
    /** @brief ["time_in_air"] Seconds since it left the ground (0 grounded or flying). */
    AnimationGraphBuiltIn_TimeInAir = 10,
    /** @brief ["turn_rate"] How fast its body turned, degrees per second (positive: toward +X from +Z). */
    AnimationGraphBuiltIn_TurnRate = 11,
    /** @brief ["look_yaw"] Where it looks relative to its body, degrees in [-180, 180] (positive: toward its left, as the
     *  yaw grows). */
    AnimationGraphBuiltIn_LookYaw = 12,
    /** @brief ["look_pitch"] Where it looks up (positive) or down, degrees. */
    AnimationGraphBuiltIn_LookPitch = 13,
    /** @brief ["is_stand_up_blocked"] Whether it stays crouched only for lack of room. */
    AnimationGraphBuiltIn_IsStandUpBlocked = 14,
    /** @brief ["landing_speed"] The impact speed of its last landing, metres per second downward (kept until the next). */
    AnimationGraphBuiltIn_LandingSpeed = 15,
    /** @brief ["is_in_action"] Whether an action of the graph plays (or approaches its anchor). */
    AnimationGraphBuiltIn_IsInAction = 16,
    /** @brief ["move_amount"] How much its driver asks it to move, 0 to 1 (the input's wish, not its speed). */
    AnimationGraphBuiltIn_MoveAmount = 17,
    /** @brief How many built-in parameters there are (the first custom parameter's index). */
    AnimationGraphBuiltIn_Count = 18
} AnimationGraphBuiltIn;

/**
 * @brief The built-in events, in this order the first event indices of every graph: the character's events of the last
 *        tick (WorldCharacters.h; the same order) and an action's end request. Custom triggers follow them.
 */
typedef enum AnimationGraphEventEnum
{
    /** @brief ["jumped"] It jumped. */
    AnimationGraphEvent_Jumped = 0,
    /** @brief ["landed"] It landed on walkable ground (landing_speed says how hard). */
    AnimationGraphEvent_Landed = 1,
    /** @brief ["left_ground"] It left the ground without jumping. */
    AnimationGraphEvent_LeftGround = 2,
    /** @brief ["crouched"] It crouched. */
    AnimationGraphEvent_Crouched = 3,
    /** @brief ["stood_up"] It stood up. */
    AnimationGraphEvent_StoodUp = 4,
    /** @brief ["took_off"] It started flying. */
    AnimationGraphEvent_TookOff = 5,
    /** @brief ["stopped_flying"] It stopped flying. */
    AnimationGraphEvent_StoppedFlying = 6,
    /** @brief ["used"] It used something. */
    AnimationGraphEvent_Used = 7,
    /** @brief ["end_action"] Whoever drives it asked the action it plays to end (WorldAnimation_EndAction): the action's
     *  states move on to their way out on it. */
    AnimationGraphEvent_EndAction = 8,
    /** @brief How many built-in events there are (the first custom trigger's index). */
    AnimationGraphEvent_Count = 9
} AnimationGraphEvent;

/**
 * @brief What a custom parameter holds. The values are stable.
 */
typedef enum AnimationGraphValueTypeEnum
{
    /** @brief A number ("float"). */
    AnimationGraphValueType_Float = 0,
    /** @brief true or false, read as 1 or 0 ("bool"). */
    AnimationGraphValueType_Bool = 1,
    /** @brief An event that code fires: true for the one tick after ("trigger"). */
    AnimationGraphValueType_Trigger = 2
} AnimationGraphValueType;

/**
 * @brief What a condition tests. The values are stable.
 */
typedef enum AnimationGraphConditionKindEnum
{
    /** @brief A parameter against a value ({"parameter": ..., "above"/"below"/"at_least"/"at_most"/"is": ...}). */
    AnimationGraphConditionKind_Parameter = 0,
    /** @brief An event of this tick ({"event": ...}). */
    AnimationGraphConditionKind_Event = 1,
    /** @brief Whether something hangs on a socket of the object ({"socket": ..., "is_holding": true/false}). */
    AnimationGraphConditionKind_Socket = 2
} AnimationGraphConditionKind;

/**
 * @brief How a parameter condition compares. The values are stable.
 */
typedef enum AnimationGraphCompareEnum
{
    /** @brief The parameter is greater than the value ("above"). */
    AnimationGraphCompare_Above = 0,
    /** @brief The parameter is less than the value ("below"). */
    AnimationGraphCompare_Below = 1,
    /** @brief The parameter is at least the value ("at_least"). */
    AnimationGraphCompare_AtLeast = 2,
    /** @brief The parameter is at most the value ("at_most"). */
    AnimationGraphCompare_AtMost = 3,
    /** @brief A boolean parameter is true (value 1) or false (value 0) ("is"). */
    AnimationGraphCompare_Is = 4
} AnimationGraphCompare;

/**
 * @brief What a state plays. The values are stable.
 */
typedef enum AnimationGraphStateKindEnum
{
    /** @brief Nothing: the layer adds nothing while in it (a body layer shows the rest pose). */
    AnimationGraphStateKind_Empty = 0,
    /** @brief One clip ("clip"). */
    AnimationGraphStateKind_Clip = 1,
    /** @brief Clips blended by a parameter, kept in step by phase ("blend"). */
    AnimationGraphStateKind_Blend = 2
} AnimationGraphStateKind;

/**
 * @brief How much of a character's movement an action locks while it plays. The values are stable.
 */
typedef enum AnimationGraphMovementLockEnum
{
    /** @brief Nothing: it moves and turns as its driver asks ("free"). */
    AnimationGraphMovementLock_Free = 0,
    /** @brief It turns but does not move, jump, crouch or fly ("no_moving"). */
    AnimationGraphMovementLock_NoMoving = 1,
    /** @brief It moves but its body does not turn ("no_turning"). */
    AnimationGraphMovementLock_NoTurning = 2,
    /** @brief Neither: it stays where the action put it ("locked"). Its look still turns its head. */
    AnimationGraphMovementLock_Locked = 3
} AnimationGraphMovementLock;

/**
 * @brief Whether an action needs a target object (what it is done to or with). The values are stable.
 */
typedef enum AnimationGraphTargetRuleEnum
{
    /** @brief It takes none ("none"). */
    AnimationGraphTargetRule_None = 0,
    /** @brief It may have one ("optional"). */
    AnimationGraphTargetRule_Optional = 1,
    /** @brief It must have one: an anchored action, or one whose events attach its target ("required"). */
    AnimationGraphTargetRule_Required = 2
} AnimationGraphTargetRule;


// Types: the block's records. Read-only; every "offset" is a string's (AnimationGraph_GetString), every index local to
// the array its accessor names.
/**
 * @brief A custom parameter.
 */
typedef struct AnimationGraphParameterStruct
{
    /** @brief Its name (a string offset). */
    uint32_t _name;
    /** @brief What it holds. */
    AnimationGraphValueType _type;
    /** @brief Its index: a parameter index (AnimationGraphBuiltIn_Count and up) for a float or a boolean, an event index
     *  (AnimationGraphEvent_Count and up) for a trigger. */
    uint32_t _index;
    /** @brief Its value at the start (booleans 0 or 1; triggers 0). */
    float _default;
    /** @brief A float's bounds (values set are clamped into them; booleans 0 and 1, triggers 0). */
    float _min;
    float _max;
} AnimationGraphParameter;

/**
 * @brief One condition of a transition.
 */
typedef struct AnimationGraphConditionStruct
{
    /** @brief What it tests. */
    AnimationGraphConditionKind _kind;
    /** @brief A parameter index, an event index, or a socket name's string offset, by _kind. */
    uint32_t _subject;
    /** @brief How a parameter compares (parameter conditions). */
    AnimationGraphCompare _compare;
    /** @brief The value it compares with (1 or 0 for "is" and for a socket's "is_holding"). */
    float _value;
} AnimationGraphCondition;

/**
 * @brief One transition of a layer: from a state (or any) to a state, when all its conditions hold.
 */
typedef struct AnimationGraphTransitionStruct
{
    /** @brief The state it leaves (an index in the layer's states), or ANIMATION_GRAPH_NONE for any state. */
    uint32_t _from;
    /** @brief The state it enters (an index in the layer's states). */
    uint32_t _to;
    /** @brief Its conditions: _conditionCount of them from _firstCondition (AnimationGraph_GetCondition). */
    uint32_t _firstCondition;
    uint32_t _conditionCount;
    /** @brief Whether it waits for the state it leaves to have played _exitTime of its cycle (1: once through; for a
     *  clip that does not loop, until it ended). */
    bool _hasExitTime;
    float _exitTime;
    /** @brief How long it cross-fades, in seconds. */
    float _fadeSeconds;
    /** @brief Whether the state it enters starts over even when it is still playing (fading out), and whether a
     *  transition from any state fires while the layer is already in _to. */
    bool _isRestarted;
} AnimationGraphTransition;

/**
 * @brief One clip of a blend: pure at a parameter value, mixed with its neighbours between.
 */
typedef struct AnimationGraphBlendPointStruct
{
    /** @brief The clip's name (a string offset). */
    uint32_t _clip;
    /** @brief The parameter's value where this clip plays alone (the points rise strictly). */
    float _at;
} AnimationGraphBlendPoint;

/**
 * @brief One parameter an event sets.
 */
typedef struct AnimationGraphParameterSetStruct
{
    /** @brief A custom float or boolean parameter's index. */
    uint32_t _parameter;
    /** @brief The value (clamped to the parameter's bounds; booleans 0 or 1). */
    float _value;
} AnimationGraphParameterSet;

/**
 * @brief One timed event of a state: fired when the state's playback passes its time.
 */
typedef struct AnimationGraphEventRecordStruct
{
    /** @brief Its name (a string offset), queued for the game when it fires. */
    uint32_t _name;
    /** @brief Whether _at is a share of the state's cycle (a phase in [0, 1)) or seconds into its clip (one-clip states
     *  only). */
    bool _isPhase;
    float _at;
    /** @brief A socket of this object to hang the action's target on (a string offset), or ANIMATION_GRAPH_NONE. */
    uint32_t _attachSocket;
    /** @brief The target's own socket that lands on it (where it is held), or ANIMATION_GRAPH_NONE: the target's origin. */
    uint32_t _gripSocket;
    /** @brief A socket of this object whose objects go back to the world where they are (a string offset), or
     *  ANIMATION_GRAPH_NONE. */
    uint32_t _detachSocket;
    /** @brief The parameters it sets: _setCount of them from _firstSet (AnimationGraph_GetParameterSet). */
    uint32_t _firstSet;
    uint32_t _setCount;
} AnimationGraphEventRecord;

/**
 * @brief One state of a layer.
 */
typedef struct AnimationGraphStateStruct
{
    /** @brief Its name (a string offset), unique in its layer. */
    uint32_t _name;
    /** @brief What it plays. */
    AnimationGraphStateKind _kind;
    /** @brief A one-clip state's clip (a string offset). */
    uint32_t _clip;
    /** @brief A blend's parameter (a parameter index) and its points: _pointCount from _firstPoint
     *  (AnimationGraph_GetBlendPoint). */
    uint32_t _blendParameter;
    uint32_t _firstPoint;
    uint32_t _pointCount;
    /** @brief How fast it plays, times each clip's own speed (0 holds it). */
    float _speed;
    /** @brief A parameter its speed is multiplied by, or ANIMATION_GRAPH_NONE. */
    uint32_t _speedParameter;
    /** @brief A parameter of metres per second it plays at (its clips' metres per cycle make their natural speed: the
     *  state plays at that parameter over it, so the feet do not slide), or ANIMATION_GRAPH_NONE. */
    uint32_t _matchParameter;
    /** @brief The bounds of that matched rate (a multiplier of the speed above). */
    float _minRate;
    float _maxRate;
    /** @brief Whether _isLooped decides if its clips loop, instead of their libraries. */
    bool _overridesIsLooped;
    bool _isLooped;
    /** @brief How much look-at turns the head while it plays, 0 to 1. */
    float _lookAtWeight;
    /** @brief Its events: _eventCount from _firstEvent (AnimationGraph_GetEvent). */
    uint32_t _firstEvent;
    uint32_t _eventCount;
} AnimationGraphState;

/**
 * @brief One layer: a state machine over a bone mask.
 */
typedef struct AnimationGraphLayerStruct
{
    /** @brief Its name (a string offset), unique in the graph. */
    uint32_t _name;
    /** @brief The bones it drives with their subtrees: _maskBoneCount string offsets from _firstMaskBone
     *  (AnimationGraph_GetMaskBone); none: every bone. */
    uint32_t _firstMaskBone;
    uint32_t _maskBoneCount;
    /** @brief How much it counts over the layers before it (the first layer: over the rest pose), 0 to 1. */
    float _weight;
    /** @brief The state it starts in (an index in its states). */
    uint32_t _entryState;
    /** @brief Its states: _stateCount from _firstState in the graph's states (AnimationGraph_GetState). */
    uint32_t _firstState;
    uint32_t _stateCount;
    /** @brief Its transitions, in priority order (the first whose conditions hold fires): _transitionCount from
     *  _firstTransition (AnimationGraph_GetTransition). */
    uint32_t _firstTransition;
    uint32_t _transitionCount;
} AnimationGraphLayer;

/**
 * @brief One special action: an entry point into a layer's states.
 */
typedef struct AnimationGraphActionStruct
{
    /** @brief Its name (a string offset), unique in the graph. */
    uint32_t _name;
    /** @brief The layer it plays on. */
    uint32_t _layer;
    /** @brief Its states, indices in that layer: _stateCount from _firstState (AnimationGraph_GetActionState); the first
     *  is where it enters. It lasts while the layer is in one of them. */
    uint32_t _firstState;
    uint32_t _stateCount;
    /** @brief How long entering its first state cross-fades, in seconds. */
    float _fadeSeconds;
    /** @brief How much of the character's movement it locks. */
    AnimationGraphMovementLock _movementLock;
    /** @brief Whether it takes a target object. */
    AnimationGraphTargetRule _targetRule;
    /** @brief Whether it first slides the character onto a socket of its target (the anchor: its place and its facing). */
    bool _hasAnchor;
    /** @brief The anchor's socket on the target (a string offset). */
    uint32_t _anchorSocket;
    /** @brief How far the anchor may be from the character's feet when it starts, in metres. */
    float _anchorDistance;
    /** @brief How long the slide onto the anchor takes, in seconds. */
    float _approachSeconds;
} AnimationGraphAction;

/**
 * @brief One bone look-at turns.
 */
typedef struct AnimationGraphLookBoneStruct
{
    /** @brief The bone's name (a string offset). */
    uint32_t _bone;
    /** @brief Its share of the turn, in (0, 1] (the shares of a chain add up to the whole turn). */
    float _share;
} AnimationGraphLookBone;

/**
 * @brief How the graph turns its character's head toward where it looks.
 */
typedef struct AnimationGraphLookAtStruct
{
    /** @brief Whether the graph turns any bone (it names at least one). */
    bool _isEnabled;
    /** @brief The bones: _boneCount from _firstBone (AnimationGraph_GetLookBone), parents before their children. */
    uint32_t _firstBone;
    uint32_t _boneCount;
    /** @brief The farthest it turns to either side, up and down, in radians. */
    float _maxYaw;
    float _maxPitchUp;
    float _maxPitchDown;
    /** @brief How fast it turns, radians per second. */
    float _turnSpeed;
} AnimationGraphLookAt;

/**
 * @brief An animation graph: one block of records (see the file comment). Read-only; every field is the block's own
 *        layout: read it through the accessors.
 */
typedef struct AnimationGraphStruct
{
    /** @brief The block's size in bytes, this header included. */
    size_t _byteCount;
    /** @brief The default cross-fade of its transitions and actions, in seconds. */
    float _fadeSeconds;
    /** @brief How it turns its character's head. */
    AnimationGraphLookAt _lookAt;
    /** @brief How many custom parameters, and how many of them are floats or booleans (parameter indices) and triggers
     *  (event indices). */
    uint32_t _parameterCount;
    uint32_t _valueParameterCount;
    uint32_t _triggerCount;
    /** @brief How many records of each kind, and where each array starts (a byte offset from the block's start). */
    uint32_t _layerCount;
    uint32_t _stateCount;
    uint32_t _transitionCount;
    uint32_t _conditionCount;
    uint32_t _pointCount;
    uint32_t _eventCount;
    uint32_t _setCount;
    uint32_t _actionCount;
    uint32_t _actionStateCount;
    uint32_t _maskBoneCount;
    uint32_t _lookBoneCount;
    uint32_t _stringByteCount;
    uint32_t _parametersOffset;
    uint32_t _layersOffset;
    uint32_t _statesOffset;
    uint32_t _transitionsOffset;
    uint32_t _conditionsOffset;
    uint32_t _pointsOffset;
    uint32_t _eventsOffset;
    uint32_t _setsOffset;
    uint32_t _actionsOffset;
    uint32_t _actionStatesOffset;
    uint32_t _maskBonesOffset;
    uint32_t _lookBonesOffset;
    uint32_t _stringsOffset;
} AnimationGraph;


// Functions: the vocabulary.
/**
 * @brief Returns a built-in parameter's JSON name ("speed", ...).
 * @param parameter The parameter; one past the enum reads NULL.
 * @returns The name (static), or NULL.
 */
const unsigned char* AnimationGraph_GetBuiltInName(AnimationGraphBuiltIn parameter);

/**
 * @brief Returns whether a built-in parameter is a boolean (read as 0 or 1).
 * @param parameter The parameter.
 * @returns Whether it is a boolean (false for a float or a value past the enum).
 */
bool AnimationGraph_IsBuiltInBoolean(AnimationGraphBuiltIn parameter);

/**
 * @brief Returns a built-in event's JSON name ("jumped", ...).
 * @param event The event; one past the enum reads NULL.
 * @returns The name (static), or NULL.
 */
const unsigned char* AnimationGraph_GetBuiltInEventName(AnimationGraphEvent event);


// Functions: making and freeing.
/**
 * @brief Reads a graph from a definition's JSON object (every key but "name", which the asset type reads), checking
 *        every rule of references/asset_structure.md, "Animation graphs": unknown keys are refused at every level, names
 *        are [a-z][a-z0-9_.-]* and unique where they must be, references (states, layers, parameters) resolve, values
 *        are within their ranges and the limits in this header hold.
 * @param root The definition's JSON object; must not be NULL.
 * @param graphName The graph's name, for messages; may be NULL.
 * @param outGraph [out] Receives the graph (one block, owned by the caller: AnimationGraph_Destroy); NULL on failure.
 * @returns Success; ErrorCode_IllegalArgument for NULL arguments; ErrorCode_InvalidAssetDefinition naming the key and
 *          the rule for a definition that breaks one; ErrorCode_BufferTooSmall when the block could not be built.
 */
Error AnimationGraph_CreateFromJSON(JSONCompound* root, const unsigned char* graphName, AnimationGraph** outGraph);

/**
 * @brief Copies a graph: one allocation of its block's bytes (it holds no pointers).
 * @param self The graph; must not be NULL.
 * @returns The copy (owned by the caller: AnimationGraph_Destroy).
 */
AnimationGraph* AnimationGraph_Clone(const AnimationGraph* self);

/**
 * @brief Frees a graph made by AnimationGraph_CreateFromJSON or AnimationGraph_Clone.
 * @param self The graph, or NULL (nothing happens).
 */
void AnimationGraph_Destroy(AnimationGraph* self);


// Functions: lookups (linear in the records they search; a graph is small, and bindings resolve names once).
/**
 * @brief Finds a parameter by name: a built-in one, or a custom float or boolean.
 * @param self The graph; must not be NULL.
 * @param name The name; NULL finds nothing.
 * @param outIndex [out] Receives its parameter index when found; may be NULL.
 * @returns Whether a parameter (not a trigger) has that name.
 */
bool AnimationGraph_TryFindParameter(const AnimationGraph* self, const unsigned char* name, uint32_t* outIndex);

/**
 * @brief Finds an event by name: a built-in one, or a custom trigger.
 * @param self The graph; must not be NULL.
 * @param name The name; NULL finds nothing.
 * @param outIndex [out] Receives its event index when found; may be NULL.
 * @returns Whether an event has that name.
 */
bool AnimationGraph_TryFindEvent(const AnimationGraph* self, const unsigned char* name, uint32_t* outIndex);

/**
 * @brief Finds a custom parameter's record by name (whatever its type).
 * @param self The graph; must not be NULL.
 * @param name The name; NULL finds nothing.
 * @returns The record (part of the graph), or NULL.
 */
const AnimationGraphParameter* AnimationGraph_FindCustomParameter(const AnimationGraph* self, const unsigned char* name);

/**
 * @brief Finds a layer by name.
 * @param self The graph; must not be NULL.
 * @param name The name; NULL finds nothing.
 * @param outIndex [out] Receives its index when found; may be NULL.
 * @returns Whether a layer has that name.
 */
bool AnimationGraph_TryFindLayer(const AnimationGraph* self, const unsigned char* name, uint32_t* outIndex);

/**
 * @brief Finds a state of a layer by name.
 * @param self The graph; must not be NULL.
 * @param layer The layer's index.
 * @param name The name; NULL finds nothing.
 * @param outIndex [out] Receives its index in the layer when found; may be NULL.
 * @returns Whether the layer has a state of that name.
 */
bool AnimationGraph_TryFindState(const AnimationGraph* self, uint32_t layer, const unsigned char* name,
    uint32_t* outIndex);

/**
 * @brief Finds an action by name.
 * @param self The graph; must not be NULL.
 * @param name The name; NULL finds nothing.
 * @param outIndex [out] Receives its index when found; may be NULL.
 * @returns Whether an action has that name.
 */
bool AnimationGraph_TryFindAction(const AnimationGraph* self, const unsigned char* name, uint32_t* outIndex);

/**
 * @brief Returns how many parameters a graph's instances hold: the built-in ones and the custom floats and booleans.
 * @param self The graph; must not be NULL.
 * @returns The count.
 */
static inline uint32_t AnimationGraph_GetParameterSlotCount(const AnimationGraph* self)
{
    return (uint32_t)AnimationGraphBuiltIn_Count + self->_valueParameterCount;
}

/**
 * @brief Returns how many events a graph knows: the built-in ones and its triggers (at most 64).
 * @param self The graph; must not be NULL.
 * @returns The count.
 */
static inline uint32_t AnimationGraph_GetEventSlotCount(const AnimationGraph* self)
{
    return (uint32_t)AnimationGraphEvent_Count + self->_triggerCount;
}


// Functions: accessors (each index below its count; the records are part of the graph).
/** @brief Returns a string of the graph by its offset (NUL-terminated). @param self The graph. @param offset A record's
 *  string offset. */
static inline const unsigned char* AnimationGraph_GetString(const AnimationGraph* self, uint32_t offset)
{
    return (const unsigned char*)self + self->_stringsOffset + offset;
}

/** @brief Returns the default cross-fade, in seconds. @param self The graph. */
static inline float AnimationGraph_GetFadeSeconds(const AnimationGraph* self)
{
    return self->_fadeSeconds;
}

/** @brief Returns how the graph turns its character's head. @param self The graph. */
static inline const AnimationGraphLookAt* AnimationGraph_GetLookAt(const AnimationGraph* self)
{
    return &self->_lookAt;
}

/** @brief Returns how many custom parameters the graph declares. @param self The graph. */
static inline uint32_t AnimationGraph_GetCustomParameterCount(const AnimationGraph* self)
{
    return self->_parameterCount;
}

/** @brief Returns a custom parameter. @param self The graph. @param index Below AnimationGraph_GetCustomParameterCount. */
static inline const AnimationGraphParameter* AnimationGraph_GetCustomParameter(const AnimationGraph* self,
    uint32_t index)
{
    return (const AnimationGraphParameter*)(const void*)((const unsigned char*)self + self->_parametersOffset) + index;
}

/** @brief Returns how many layers the graph has (at least 1). @param self The graph. */
static inline uint32_t AnimationGraph_GetLayerCount(const AnimationGraph* self)
{
    return self->_layerCount;
}

/** @brief Returns a layer. @param self The graph. @param index Below AnimationGraph_GetLayerCount. */
static inline const AnimationGraphLayer* AnimationGraph_GetLayer(const AnimationGraph* self, uint32_t index)
{
    return (const AnimationGraphLayer*)(const void*)((const unsigned char*)self + self->_layersOffset) + index;
}

/** @brief Returns how many states all layers have together. @param self The graph. */
static inline uint32_t AnimationGraph_GetStateCount(const AnimationGraph* self)
{
    return self->_stateCount;
}

/** @brief Returns a state by its index among all layers' states (a layer's start at its _firstState). @param self The
 *  graph. @param index Below AnimationGraph_GetStateCount. */
static inline const AnimationGraphState* AnimationGraph_GetState(const AnimationGraph* self, uint32_t index)
{
    return (const AnimationGraphState*)(const void*)((const unsigned char*)self + self->_statesOffset) + index;
}

/** @brief Returns a layer's state by its index in the layer. @param self The graph. @param layer The layer. @param
 *  state Below its _stateCount. */
static inline const AnimationGraphState* AnimationGraph_GetLayerState(const AnimationGraph* self,
    const AnimationGraphLayer* layer, uint32_t state)
{
    return AnimationGraph_GetState(self, layer->_firstState + state);
}

/** @brief Returns a transition by its index among all layers' transitions. @param self The graph. @param index A
 *  layer's _firstTransition plus one below its _transitionCount. */
static inline const AnimationGraphTransition* AnimationGraph_GetTransition(const AnimationGraph* self, uint32_t index)
{
    return (const AnimationGraphTransition*)(const void*)((const unsigned char*)self + self->_transitionsOffset) + index;
}

/** @brief Returns a condition by its index among all conditions. @param self The graph. @param index A transition's
 *  _firstCondition plus one below its _conditionCount. */
static inline const AnimationGraphCondition* AnimationGraph_GetCondition(const AnimationGraph* self, uint32_t index)
{
    return (const AnimationGraphCondition*)(const void*)((const unsigned char*)self + self->_conditionsOffset) + index;
}

/** @brief Returns a blend point by its index among all points. @param self The graph. @param index A state's
 *  _firstPoint plus one below its _pointCount. */
static inline const AnimationGraphBlendPoint* AnimationGraph_GetBlendPoint(const AnimationGraph* self, uint32_t index)
{
    return (const AnimationGraphBlendPoint*)(const void*)((const unsigned char*)self + self->_pointsOffset) + index;
}

/** @brief Returns an event record by its index among all events. @param self The graph. @param index A state's
 *  _firstEvent plus one below its _eventCount. */
static inline const AnimationGraphEventRecord* AnimationGraph_GetEvent(const AnimationGraph* self, uint32_t index)
{
    return (const AnimationGraphEventRecord*)(const void*)((const unsigned char*)self + self->_eventsOffset) + index;
}

/** @brief Returns a parameter set by its index among all sets. @param self The graph. @param index An event's _firstSet
 *  plus one below its _setCount. */
static inline const AnimationGraphParameterSet* AnimationGraph_GetParameterSet(const AnimationGraph* self,
    uint32_t index)
{
    return (const AnimationGraphParameterSet*)(const void*)((const unsigned char*)self + self->_setsOffset) + index;
}

/** @brief Returns how many actions the graph has. @param self The graph. */
static inline uint32_t AnimationGraph_GetActionCount(const AnimationGraph* self)
{
    return self->_actionCount;
}

/** @brief Returns an action. @param self The graph. @param index Below AnimationGraph_GetActionCount. */
static inline const AnimationGraphAction* AnimationGraph_GetAction(const AnimationGraph* self, uint32_t index)
{
    return (const AnimationGraphAction*)(const void*)((const unsigned char*)self + self->_actionsOffset) + index;
}

/** @brief Returns one of an action's states (an index in its layer). @param self The graph. @param action The action.
 *  @param index Below its _stateCount. */
static inline uint32_t AnimationGraph_GetActionState(const AnimationGraph* self, const AnimationGraphAction* action,
    uint32_t index)
{
    return ((const uint32_t*)(const void*)((const unsigned char*)self + self->_actionStatesOffset))[action->_firstState
        + index];
}

/** @brief Returns a layer's mask bone name. @param self The graph. @param layer The layer. @param index Below its
 *  _maskBoneCount. */
static inline const unsigned char* AnimationGraph_GetMaskBone(const AnimationGraph* self,
    const AnimationGraphLayer* layer, uint32_t index)
{
    uint32_t Offset = ((const uint32_t*)(const void*)((const unsigned char*)self + self->_maskBonesOffset))[
        layer->_firstMaskBone + index];
    return AnimationGraph_GetString(self, Offset);
}

/** @brief Returns a bone look-at turns. @param self The graph. @param index Below the look-at's _boneCount. */
static inline const AnimationGraphLookBone* AnimationGraph_GetLookBone(const AnimationGraph* self, uint32_t index)
{
    return (const AnimationGraphLookBone*)(const void*)((const unsigned char*)self + self->_lookBonesOffset)
        + self->_lookAt._firstBone + index;
}
