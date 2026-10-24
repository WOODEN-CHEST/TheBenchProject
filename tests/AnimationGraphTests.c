#include "TestSuites.h"
#include "TestAssets.h"
#include "TestText.h"
#include "AnimationGraph.h"
#include "AssetManager.h"
#include "wr/WRJSON.h"
#include "wr/WRMemory.h"
#include <math.h>
#include <stdio.h>
#include <string.h>


// Macros.
/** Tolerance for numbers read from the definitions (and turned into radians). */
#define TEST_EPSILON 1e-5f
/** Room for a definition written by a test. */
#define GRAPH_TEXT_CAPACITY 65536


// Types.
/* A JSON pool and the root of the last definition parsed with it. */
typedef struct GraphJsonStruct
{
    JSONObjectPool* Pool;
    JSONObjectValue Root;
    bool HasRoot;
} GraphJson;


// Fields.
/* A graph that uses every part of the format (the single quotes become double ones). */
static const char* const FullGraph =
    "{'name': 'full', 'fade_seconds': 0.3,"
    " 'parameters': {"
    "   'mood': {'type': 'float', 'default': 0.5, 'min': -1, 'max': 2},"
    "   'is_tired': {'type': 'bool', 'default': true},"
    "   'wave_now': {'type': 'trigger'},"
    "   'pace': {'type': 'float', 'min': 1, 'max': 3}"
    " },"
    " 'layers': ["
    "   {'name': 'body', 'entry': 'locomotion',"
    "    'states': {"
    "      'locomotion': {'blend': {'parameter': 'speed', 'points': [{'clip': 'idle', 'at': 0}, {'clip': 'walk', 'at': 2},"
    "        {'clip': 'run', 'at': 5}]}, 'match_speed': 'speed', 'min_rate': 0.5, 'max_rate': 2.5, 'look_at': 0.75,"
    "        'events': [{'name': 'footstep', 'phase': 0.0}, {'name': 'footstep', 'phase': 0.5}]},"
    "      'jump': {'clip': 'jump', 'speed': 1.5, 'is_looped': false},"
    "      'pick_up': {'clip': 'pick_up', 'speed_parameter': 'pace', 'events': [{'name': 'grab', 'time': 0.4,"
    "        'attach': 'hand_r', 'grip': 'handle', 'set': {'is_tired': false, 'mood': 1.5}}]},"
    "      'put_down': {'clip': 'put_down', 'events': [{'name': 'release', 'time': 0.5, 'detach': 'hand_r'}]},"
    "      'nothing': {}"
    "    },"
    "    'transitions': ["
    "      {'from': 'any', 'to': 'jump', 'when': [{'event': 'jumped'}], 'fade_seconds': 0.1},"
    "      {'from': ['jump', 'pick_up', 'put_down'], 'to': 'locomotion', 'exit_time': 1},"
    "      {'from': 'locomotion', 'to': 'nothing', 'when': [{'parameter': 'speed', 'above': 9},"
    "        {'parameter': 'is_tired', 'is': true}, {'socket': 'hand_r', 'is_holding': false},"
    "        {'event': 'wave_now'}, {'parameter': 'mood', 'at_most': -0.5}]},"
    "      {'from': 'jump', 'to': 'jump', 'when': [{'event': 'jumped'}], 'restarts': true}"
    "    ]},"
    "   {'name': 'arms', 'mask': ['upper_arm_r', 'upper_arm_l'], 'weight': 0.8, 'entry': 'free',"
    "    'states': {'free': {}, 'hold': {'clip': 'hold_r'}},"
    "    'transitions': [{'from': 'free', 'to': 'hold', 'when': [{'socket': 'hand_r', 'is_holding': true}]},"
    "      {'from': 'hold', 'to': 'free', 'when': [{'socket': 'hand_r', 'is_holding': false}], 'fade_seconds': 0}]}"
    " ],"
    " 'look_at': {'bones': [{'bone': 'neck', 'share': 0.4}, {'bone': 'head', 'share': 0.6}], 'max_yaw': 90,"
    "   'max_pitch_up': 30, 'max_pitch_down': 45, 'turn_speed': 180},"
    " 'actions': {"
    "   'pick_up': {'layer': 'body', 'states': ['pick_up'], 'movement': 'no_moving', 'target': 'required'},"
    "   'sit': {'layer': 'body', 'states': ['nothing', 'jump'], 'fade_seconds': 0.5, 'movement': 'locked',"
    "     'anchor': {'socket': 'seat', 'max_distance': 2, 'approach_seconds': 0.25}},"
    "   'wave': {'layer': 'arms', 'states': ['hold']}"
    " }"
    "}";


// Static functions: helpers.
static bool IsNear(float a, float b)
{
    return fabsf(a - b) <= TEST_EPSILON;
}

static bool IsText(const AnimationGraph* graph, uint32_t offset, const char* expected)
{
    return strcmp((const char*)AnimationGraph_GetString(graph, offset), expected) == 0;
}

/* Copies @p text with every single quote turned into a double one. */
static void Unquote(const char* text, char* buffer, size_t capacity)
{
    size_t Index = 0U;
    for (; (text[Index] != '\0') && (Index < (capacity - 1U)); Index++)
    {
        buffer[Index] = (text[Index] == '\'') ? '"' : text[Index];
    }
    buffer[Index] = '\0';
}

static bool GraphJson_Construct(TestRun* run, GraphJson* self)
{
    Memory_Zero(self, sizeof(*self));
    return TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&self->Pool));
}

static void GraphJson_ReleaseRoot(TestRun* run, GraphJson* self)
{
    if (self->HasRoot)
    {
        TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(self->Pool, &self->Root));
        self->HasRoot = false;
    }
}

static void GraphJson_Deconstruct(TestRun* run, GraphJson* self)
{
    GraphJson_ReleaseRoot(run, self);
    JSONObjectPool_Deconstruct(self->Pool);
}

/* Parses a definition (single quotes for double ones) into the root. */
static JSONCompound* GraphJson_Parse(TestRun* run, GraphJson* self, const char* text)
{
    static char Text[GRAPH_TEXT_CAPACITY];
    GraphJson_ReleaseRoot(run, self);
    Unquote(text, Text, sizeof(Text));
    if (!TestText_ParseJSON(run, self->Pool, Text, &self->Root))
    {
        return NULL;
    }
    self->HasRoot = true;
    return TEST_CHECK(run, self->Root.Type == JSONValueType_Compound) ? self->Root.Value.Compound : NULL;
}

/* Reads a graph from a definition (single quotes for double ones); NULL with a failed check when it is refused. */
static AnimationGraph* ReadGraph(TestRun* run, GraphJson* json, const char* text)
{
    JSONCompound* Root = GraphJson_Parse(run, json, text);
    AnimationGraph* Graph = NULL;
    if ((Root != NULL) && TEST_CHECK_SUCCESS(run, AnimationGraph_CreateFromJSON(Root, (const unsigned char*)u8"test",
        &Graph)))
    {
        return Graph;
    }
    return NULL;
}

/* Checks a definition is refused as invalid with every fragment in its message. */
static void CheckRefused(TestRun* run, GraphJson* json, const char* text, const char* fragment)
{
    JSONCompound* Root = GraphJson_Parse(run, json, text);
    if (Root == NULL)
    {
        return;
    }
    AnimationGraph* Graph = NULL;
    const char* Fragments[] = { fragment, NULL };
    if (!TestText_CheckErrorMessage(run, AnimationGraph_CreateFromJSON(Root, (const unsigned char*)u8"test", &Graph),
        ErrorCode_InvalidAssetDefinition, Fragments))
    {
        printf("    refused wrongly: %s\n", text);
    }
    TEST_CHECK(run, Graph == NULL);
}

/* A layer's state by name. */
static const AnimationGraphState* FindState(const AnimationGraph* graph, uint32_t layer, const char* name)
{
    uint32_t Index = 0U;
    if (!AnimationGraph_TryFindState(graph, layer, (const unsigned char*)name, &Index))
    {
        return NULL;
    }
    return AnimationGraph_GetLayerState(graph, AnimationGraph_GetLayer(graph, layer), Index);
}


// Static functions: tests.
static void AFullGraphReadsEveryPart(TestRun* run)
{
    GraphJson Json;
    if (!GraphJson_Construct(run, &Json))
    {
        return;
    }
    AnimationGraph* Graph = ReadGraph(run, &Json, FullGraph);
    if (Graph != NULL)
    {
        TEST_CHECK(run, IsNear(AnimationGraph_GetFadeSeconds(Graph), 0.3f));
        // Parameters: floats and booleans after the built-in ones, in their order; triggers after the built-in events.
        TEST_CHECK(run, (AnimationGraph_GetCustomParameterCount(Graph) == 4U)
            && (AnimationGraph_GetParameterSlotCount(Graph) == (uint32_t)AnimationGraphBuiltIn_Count + 3U)
            && (AnimationGraph_GetEventSlotCount(Graph) == (uint32_t)AnimationGraphEvent_Count + 1U));
        const AnimationGraphParameter* Mood = AnimationGraph_FindCustomParameter(Graph, (const unsigned char*)u8"mood");
        const AnimationGraphParameter* Tired = AnimationGraph_FindCustomParameter(Graph,
            (const unsigned char*)u8"is_tired");
        const AnimationGraphParameter* WaveNow = AnimationGraph_FindCustomParameter(Graph,
            (const unsigned char*)u8"wave_now");
        const AnimationGraphParameter* Pace = AnimationGraph_FindCustomParameter(Graph, (const unsigned char*)u8"pace");
        TEST_CHECK(run, (Mood != NULL) && (Mood->_type == AnimationGraphValueType_Float)
            && (Mood->_index == (uint32_t)AnimationGraphBuiltIn_Count) && IsNear(Mood->_default, 0.5f)
            && IsNear(Mood->_min, -1.0f) && IsNear(Mood->_max, 2.0f));
        TEST_CHECK(run, (Tired != NULL) && (Tired->_type == AnimationGraphValueType_Bool)
            && (Tired->_index == (uint32_t)AnimationGraphBuiltIn_Count + 1U) && IsNear(Tired->_default, 1.0f));
        TEST_CHECK(run, (WaveNow != NULL) && (WaveNow->_type == AnimationGraphValueType_Trigger)
            && (WaveNow->_index == (uint32_t)AnimationGraphEvent_Count));
        // Without a default a float starts at 0, or at its bound nearest 0.
        TEST_CHECK(run, (Pace != NULL) && IsNear(Pace->_default, 1.0f));
        uint32_t Index = 0U;
        TEST_CHECK(run, AnimationGraph_TryFindParameter(Graph, (const unsigned char*)u8"crouch_amount", &Index)
            && (Index == (uint32_t)AnimationGraphBuiltIn_CrouchAmount));
        TEST_CHECK(run, AnimationGraph_TryFindParameter(Graph, (const unsigned char*)u8"is_tired", &Index)
            && (Index == Tired->_index));
        TEST_CHECK(run, !AnimationGraph_TryFindParameter(Graph, (const unsigned char*)u8"wave_now", NULL)
            && AnimationGraph_TryFindEvent(Graph, (const unsigned char*)u8"wave_now", &Index) && (Index == WaveNow->_index));
        TEST_CHECK(run, AnimationGraph_TryFindEvent(Graph, (const unsigned char*)u8"landed", &Index)
            && (Index == (uint32_t)AnimationGraphEvent_Landed) && !AnimationGraph_TryFindEvent(Graph,
            (const unsigned char*)u8"mood", NULL));

        // Layers.
        TEST_CHECK(run, AnimationGraph_GetLayerCount(Graph) == 2U);
        uint32_t Arms = 0U;
        TEST_CHECK(run, AnimationGraph_TryFindLayer(Graph, (const unsigned char*)u8"arms", &Arms) && (Arms == 1U));
        const AnimationGraphLayer* Body = AnimationGraph_GetLayer(Graph, 0U);
        const AnimationGraphLayer* ArmsLayer = AnimationGraph_GetLayer(Graph, 1U);
        TEST_CHECK(run, IsText(Graph, Body->_name, "body") && (Body->_stateCount == 5U) && IsNear(Body->_weight, 1.0f)
            && (Body->_maskBoneCount == 0U) && (Body->_transitionCount == 6U));
        TEST_CHECK(run, (ArmsLayer->_maskBoneCount == 2U) && IsNear(ArmsLayer->_weight, 0.8f)
            && (strcmp((const char*)AnimationGraph_GetMaskBone(Graph, ArmsLayer, 1U), "upper_arm_l") == 0)
            && (ArmsLayer->_stateCount == 2U) && (ArmsLayer->_firstState == 5U));
        uint32_t Entry = 0U;
        TEST_CHECK(run, AnimationGraph_TryFindState(Graph, 0U, (const unsigned char*)u8"locomotion", &Entry)
            && (Body->_entryState == Entry));

        // States: a blend, a clip with its playback, events with their effects, an empty state.
        const AnimationGraphState* Locomotion = FindState(Graph, 0U, "locomotion");
        if (TEST_CHECK(run, Locomotion != NULL))
        {
            TEST_CHECK(run, (Locomotion->_kind == AnimationGraphStateKind_Blend)
                && (Locomotion->_blendParameter == (uint32_t)AnimationGraphBuiltIn_Speed) && (Locomotion->_pointCount == 3U)
                && (Locomotion->_matchParameter == (uint32_t)AnimationGraphBuiltIn_Speed)
                && IsNear(Locomotion->_minRate, 0.5f) && IsNear(Locomotion->_maxRate, 2.5f)
                && IsNear(Locomotion->_lookAtWeight, 0.75f) && (Locomotion->_eventCount == 2U));
            const AnimationGraphBlendPoint* Run = AnimationGraph_GetBlendPoint(Graph, Locomotion->_firstPoint + 2U);
            TEST_CHECK(run, IsText(Graph, Run->_clip, "run") && IsNear(Run->_at, 5.0f));
            const AnimationGraphEventRecord* Second = AnimationGraph_GetEvent(Graph, Locomotion->_firstEvent + 1U);
            TEST_CHECK(run, IsText(Graph, Second->_name, "footstep") && Second->_isPhase && IsNear(Second->_at, 0.5f)
                && (Second->_attachSocket == ANIMATION_GRAPH_NONE) && (Second->_setCount == 0U));
        }
        const AnimationGraphState* Jump = FindState(Graph, 0U, "jump");
        TEST_CHECK(run, (Jump != NULL) && (Jump->_kind == AnimationGraphStateKind_Clip) && IsText(Graph, Jump->_clip,
            "jump") && IsNear(Jump->_speed, 1.5f) && Jump->_overridesIsLooped && !Jump->_isLooped
            && (Jump->_matchParameter == ANIMATION_GRAPH_NONE) && IsNear(Jump->_lookAtWeight, 1.0f));
        const AnimationGraphState* PickUp = FindState(Graph, 0U, "pick_up");
        if (TEST_CHECK(run, (PickUp != NULL) && (PickUp->_eventCount == 1U)))
        {
            TEST_CHECK(run, PickUp->_speedParameter == Pace->_index);
            const AnimationGraphEventRecord* Grab = AnimationGraph_GetEvent(Graph, PickUp->_firstEvent);
            TEST_CHECK(run, !Grab->_isPhase && IsNear(Grab->_at, 0.4f) && IsText(Graph, Grab->_attachSocket, "hand_r")
                && IsText(Graph, Grab->_gripSocket, "handle") && (Grab->_detachSocket == ANIMATION_GRAPH_NONE)
                && (Grab->_setCount == 2U));
            const AnimationGraphParameterSet* First = AnimationGraph_GetParameterSet(Graph, Grab->_firstSet);
            const AnimationGraphParameterSet* Second = AnimationGraph_GetParameterSet(Graph, Grab->_firstSet + 1U);
            TEST_CHECK(run, (First->_parameter == Tired->_index) && IsNear(First->_value, 0.0f)
                && (Second->_parameter == Mood->_index) && IsNear(Second->_value, 1.5f));
        }
        const AnimationGraphState* PutDown = FindState(Graph, 0U, "put_down");
        TEST_CHECK(run, (PutDown != NULL) && IsText(Graph, AnimationGraph_GetEvent(Graph,
            PutDown->_firstEvent)->_detachSocket, "hand_r"));
        const AnimationGraphState* Nothing = FindState(Graph, 0U, "nothing");
        TEST_CHECK(run, (Nothing != NULL) && (Nothing->_kind == AnimationGraphStateKind_Empty));

        // Transitions: "any", a list of three (one record each), every kind of condition, a restart.
        uint32_t JumpIndex = 0U;
        uint32_t PickUpIndex = 0U;
        uint32_t LocomotionIndex = 0U;
        AnimationGraph_TryFindState(Graph, 0U, (const unsigned char*)u8"jump", &JumpIndex);
        AnimationGraph_TryFindState(Graph, 0U, (const unsigned char*)u8"pick_up", &PickUpIndex);
        AnimationGraph_TryFindState(Graph, 0U, (const unsigned char*)u8"locomotion", &LocomotionIndex);
        const AnimationGraphTransition* Any = AnimationGraph_GetTransition(Graph, Body->_firstTransition);
        TEST_CHECK(run, (Any->_from == ANIMATION_GRAPH_NONE) && (Any->_to == JumpIndex) && (Any->_conditionCount == 1U)
            && IsNear(Any->_fadeSeconds, 0.1f) && !Any->_hasExitTime && !Any->_isRestarted);
        const AnimationGraphCondition* Jumped = AnimationGraph_GetCondition(Graph, Any->_firstCondition);
        TEST_CHECK(run, (Jumped->_kind == AnimationGraphConditionKind_Event)
            && (Jumped->_subject == (uint32_t)AnimationGraphEvent_Jumped));
        const AnimationGraphTransition* FromJump = AnimationGraph_GetTransition(Graph, Body->_firstTransition + 1U);
        const AnimationGraphTransition* FromPickUp = AnimationGraph_GetTransition(Graph, Body->_firstTransition + 2U);
        TEST_CHECK(run, (FromJump->_from == JumpIndex) && (FromPickUp->_from == PickUpIndex)
            && (FromJump->_to == LocomotionIndex) && FromPickUp->_hasExitTime && IsNear(FromPickUp->_exitTime, 1.0f)
            && IsNear(FromPickUp->_fadeSeconds, 0.3f) && (FromPickUp->_conditionCount == 0U));
        const AnimationGraphTransition* Many = AnimationGraph_GetTransition(Graph, Body->_firstTransition + 4U);
        if (TEST_CHECK(run, Many->_conditionCount == 5U))
        {
            const AnimationGraphCondition* Above = AnimationGraph_GetCondition(Graph, Many->_firstCondition);
            const AnimationGraphCondition* Is = AnimationGraph_GetCondition(Graph, Many->_firstCondition + 1U);
            const AnimationGraphCondition* Socket = AnimationGraph_GetCondition(Graph, Many->_firstCondition + 2U);
            const AnimationGraphCondition* Trigger = AnimationGraph_GetCondition(Graph, Many->_firstCondition + 3U);
            const AnimationGraphCondition* AtMost = AnimationGraph_GetCondition(Graph, Many->_firstCondition + 4U);
            TEST_CHECK(run, (Above->_kind == AnimationGraphConditionKind_Parameter)
                && (Above->_compare == AnimationGraphCompare_Above) && IsNear(Above->_value, 9.0f));
            TEST_CHECK(run, (Is->_compare == AnimationGraphCompare_Is) && (Is->_subject == Tired->_index)
                && IsNear(Is->_value, 1.0f));
            TEST_CHECK(run, (Socket->_kind == AnimationGraphConditionKind_Socket) && IsText(Graph, Socket->_subject,
                "hand_r") && IsNear(Socket->_value, 0.0f));
            TEST_CHECK(run, (Trigger->_kind == AnimationGraphConditionKind_Event) && (Trigger->_subject == WaveNow->_index));
            TEST_CHECK(run, (AtMost->_compare == AnimationGraphCompare_AtMost) && IsNear(AtMost->_value, -0.5f));
        }
        const AnimationGraphTransition* Restart = AnimationGraph_GetTransition(Graph, Body->_firstTransition + 5U);
        TEST_CHECK(run, (Restart->_from == JumpIndex) && (Restart->_to == JumpIndex) && Restart->_isRestarted);
        const AnimationGraphTransition* Drop = AnimationGraph_GetTransition(Graph, ArmsLayer->_firstTransition + 1U);
        TEST_CHECK(run, IsNear(Drop->_fadeSeconds, 0.0f));

        // Look-at, in radians.
        const AnimationGraphLookAt* LookAt = AnimationGraph_GetLookAt(Graph);
        TEST_CHECK(run, LookAt->_isEnabled && (LookAt->_boneCount == 2U)
            && IsText(Graph, AnimationGraph_GetLookBone(Graph, 1U)->_bone, "head")
            && IsNear(AnimationGraph_GetLookBone(Graph, 0U)->_share, 0.4f) && IsNear(LookAt->_maxYaw, (PI / 2.0f))
            && IsNear(LookAt->_maxPitchUp, (PI / 6.0f)) && IsNear(LookAt->_maxPitchDown, (PI / 4.0f))
            && IsNear(LookAt->_turnSpeed, PI));

        // Actions: their layer and states, locks, targets (an anchor makes it required), anchors.
        TEST_CHECK(run, AnimationGraph_GetActionCount(Graph) == 3U);
        uint32_t Sit = 0U;
        if (TEST_CHECK(run, AnimationGraph_TryFindAction(Graph, (const unsigned char*)u8"sit", &Sit)))
        {
            const AnimationGraphAction* Action = AnimationGraph_GetAction(Graph, Sit);
            uint32_t NothingIndex = 0U;
            AnimationGraph_TryFindState(Graph, 0U, (const unsigned char*)u8"nothing", &NothingIndex);
            TEST_CHECK(run, (Action->_layer == 0U) && (Action->_stateCount == 2U)
                && (AnimationGraph_GetActionState(Graph, Action, 0U) == NothingIndex)
                && (AnimationGraph_GetActionState(Graph, Action, 1U) == JumpIndex) && IsNear(Action->_fadeSeconds, 0.5f)
                && (Action->_movementLock == AnimationGraphMovementLock_Locked)
                && (Action->_targetRule == AnimationGraphTargetRule_Required) && Action->_hasAnchor
                && IsText(Graph, Action->_anchorSocket, "seat") && IsNear(Action->_anchorDistance, 2.0f)
                && IsNear(Action->_approachSeconds, 0.25f));
        }
        const AnimationGraphAction* Waving = AnimationGraph_GetAction(Graph, 2U);
        TEST_CHECK(run, IsText(Graph, Waving->_name, "wave") && (Waving->_layer == 1U)
            && (Waving->_movementLock == AnimationGraphMovementLock_Free)
            && (Waving->_targetRule == AnimationGraphTargetRule_Optional) && !Waving->_hasAnchor
            && IsNear(Waving->_fadeSeconds, 0.3f));
        TEST_CHECK(run, AnimationGraph_GetAction(Graph, 0U)->_movementLock == AnimationGraphMovementLock_NoMoving);
        AnimationGraph_Destroy(Graph);
    }
    GraphJson_Deconstruct(run, &Json);
}

static void DefaultsFillWhatAGraphLeavesOut(TestRun* run)
{
    GraphJson Json;
    if (!GraphJson_Construct(run, &Json))
    {
        return;
    }
    AnimationGraph* Graph = ReadGraph(run, &Json, "{'name': 'small', 'layers': [{'name': 'body', 'entry': 'idle', "
        "'states': {'idle': {'clip': 'idle'}, 'turn': {'clip': 'turn', 'match_speed': 'turn_rate'}}, 'transitions': "
        "[{'from': 'idle', 'to': 'turn'}]}], 'actions': {'spin': {'layer': 'body', 'states': ['turn']}}}");
    if (Graph != NULL)
    {
        const AnimationGraphLayer* Layer = AnimationGraph_GetLayer(Graph, 0U);
        const AnimationGraphState* Idle = FindState(Graph, 0U, "idle");
        const AnimationGraphState* Turn = FindState(Graph, 0U, "turn");
        TEST_CHECK(run, IsNear(AnimationGraph_GetFadeSeconds(Graph), ANIMATION_GRAPH_DEFAULT_FADE_SECONDS)
            && (AnimationGraph_GetCustomParameterCount(Graph) == 0U) && !AnimationGraph_GetLookAt(Graph)->_isEnabled
            && IsNear(Layer->_weight, 1.0f) && (Layer->_maskBoneCount == 0U));
        TEST_CHECK(run, (Idle != NULL) && IsNear(Idle->_speed, 1.0f) && (Idle->_speedParameter == ANIMATION_GRAPH_NONE)
            && (Idle->_matchParameter == ANIMATION_GRAPH_NONE) && !Idle->_overridesIsLooped
            && IsNear(Idle->_lookAtWeight, 1.0f) && (Idle->_eventCount == 0U));
        TEST_CHECK(run, (Turn != NULL) && (Turn->_matchParameter == (uint32_t)AnimationGraphBuiltIn_TurnRate)
            && IsNear(Turn->_minRate, ANIMATION_GRAPH_DEFAULT_MIN_RATE)
            && IsNear(Turn->_maxRate, ANIMATION_GRAPH_DEFAULT_MAX_RATE));
        const AnimationGraphTransition* Transition = AnimationGraph_GetTransition(Graph, Layer->_firstTransition);
        TEST_CHECK(run, (Transition->_conditionCount == 0U) && !Transition->_hasExitTime
            && IsNear(Transition->_fadeSeconds, ANIMATION_GRAPH_DEFAULT_FADE_SECONDS) && !Transition->_isRestarted);
        const AnimationGraphAction* Spin = AnimationGraph_GetAction(Graph, 0U);
        TEST_CHECK(run, (Spin->_movementLock == AnimationGraphMovementLock_Free)
            && (Spin->_targetRule == AnimationGraphTargetRule_Optional) && !Spin->_hasAnchor
            && IsNear(Spin->_fadeSeconds, ANIMATION_GRAPH_DEFAULT_FADE_SECONDS));
        AnimationGraph_Destroy(Graph);
    }
    // The graph's own default fade reaches the transitions and actions that set none.
    Graph = ReadGraph(run, &Json, "{'fade_seconds': 0, 'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {}, "
        "'b': {}}, 'transitions': [{'from': 'a', 'to': 'b'}]}], 'actions': {'go': {'layer': 'body', 'states': ['b']}}}");
    if (Graph != NULL)
    {
        TEST_CHECK(run, IsNear(AnimationGraph_GetTransition(Graph, 0U)->_fadeSeconds, 0.0f)
            && IsNear(AnimationGraph_GetAction(Graph, 0U)->_fadeSeconds, 0.0f));
        AnimationGraph_Destroy(Graph);
    }
    GraphJson_Deconstruct(run, &Json);
}

static void TheBlockCopiesAsBytes(TestRun* run)
{
    GraphJson Json;
    if (!GraphJson_Construct(run, &Json))
    {
        return;
    }
    AnimationGraph* Graph = ReadGraph(run, &Json, FullGraph);
    if (Graph != NULL)
    {
        AnimationGraph* Copy = AnimationGraph_Clone(Graph);
        TEST_CHECK(run, (Copy != Graph) && (Copy->_byteCount == Graph->_byteCount)
            && Memory_IsEqual(Copy, Graph, Graph->_byteCount));
        // The copy holds no pointer into the original: it reads the same once the original is gone.
        Memory_Set(Graph, 0x5A, Graph->_byteCount);
        AnimationGraph_Destroy(Graph);
        const AnimationGraphState* Locomotion = FindState(Copy, 0U, "locomotion");
        TEST_CHECK(run, (Locomotion != NULL) && IsText(Copy, AnimationGraph_GetBlendPoint(Copy,
            Locomotion->_firstPoint)->_clip, "idle"));
        TEST_CHECK(run, AnimationGraph_TryFindAction(Copy, (const unsigned char*)u8"wave", NULL));
        AnimationGraph_Destroy(Copy);
    }
    GraphJson_Deconstruct(run, &Json);
}

static void TheAssetTypeLoadsACopyOfTheDefinition(TestRun* run)
{
    TestAssetFixture Fixture;
    char Text[GRAPH_TEXT_CAPACITY];
    Unquote(FullGraph, Text, sizeof(Text));
    if (TestAssetFixture_Construct(run, &Fixture) && TestAssets_AddAnimationGraph(run, &Fixture, Text))
    {
        AssetUserID User = ASSET_USER_ID_INVALID;
        AnimationGraph* Graph = NULL;
        if (TEST_CHECK_SUCCESS(run, AssetManager_GetNewUserID(Fixture.Manager, &User))
            && TEST_CHECK_SUCCESS(run, AssetManager_LoadAnimationGraph(Fixture.Manager, (const unsigned char*)u8"full",
                User, &Graph)))
        {
            TEST_CHECK(run, (Graph != NULL) && (AnimationGraph_GetLayerCount(Graph) == 2U)
                && AnimationGraph_TryFindAction(Graph, (const unsigned char*)u8"sit", NULL));
        }
        // A broken definition is refused when it is built (at startup), naming the graph.
        GenericBuffer Bytes;
        GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
        GenericBuffer_AppendString(&Bytes, (const unsigned char*)u8"{\"name\": \"broken\", \"layers\": []}");
        AssetDefinition* Definition = NULL;
        const char* Fragments[] = { "Animation graph \"broken\"", "1 to", NULL };
        TestText_CheckErrorMessage(run, AssetManager_BuildDefinition(Fixture.Manager, Fixture.Types.AnimationGraph,
            &Bytes, (const unsigned char*)u8"test", &Definition), ErrorCode_InvalidAssetDefinition, Fragments);
        Memory_Free(Bytes._data);
        if (User != ASSET_USER_ID_INVALID)
        {
            TEST_CHECK_SUCCESS(run, AssetManager_RetireUser(Fixture.Manager, User));
        }
    }
    TestAssetFixture_Deconstruct(run, &Fixture);
}

static void BadDefinitionsAreRefusedWithTheirReason(TestRun* run)
{
    // Pieces of a valid layer the cases build on.
    static const struct
    {
        const char* Json;
        const char* Fragment;
    }
    Cases[] =
    {
        // The definition's own keys and its layers.
        { "{'layers': [], 'colour': 1}", "colour" },
        { "{}", "needs \"layers\"" },
        { "{'layers': []}", "1 to 8" },
        { "{'layers': [{'name': 'Body', 'entry': 'a', 'states': {'a': {}}}]}", "not a layer's name" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {}}}, {'name': 'body', 'entry': 'a', 'states': "
            "{'a': {}}}]}", "as layers[0] is" },
        { "{'layers': [{'name': 'body', 'entry': 'b', 'states': {'a': {}}}]}", "not a state of" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {}}]}", "holds no state" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {}}, 'weight': 1.5}]}", "weight is 1.5" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {}}, 'mask': []}]}", "1 to 32" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {}}, 'mask': ['']}]}", "a bone's name" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {}}, 'mask': ['a_bone_name_longer_than_31_bytes']}]}",
            "a bone's name" },
        // States.
        { "{'layers': [{'name': 'body', 'entry': 'any', 'states': {'any': {}}}]}", "no state may be called so" },
        { "{'layers': [{'name': 'body', 'entry': 'A', 'states': {'A': {}}}]}", "not a valid state name" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {'clip': 'x', 'blend': {}}}}]}", "not both" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {'clip': 'X'}}}]}", "not a clip's name" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {'clip': 'x', 'tempo': 2}}}]}", "tempo" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {'clip': 'x', 'speed': -1}}}]}", "speed is -1" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {'clip': 'x', 'speed_parameter': 'nope'}}}]}",
            "neither a built-in parameter" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {'match_speed': 'speed'}}}]}",
            "no clip to match" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {'clip': 'x', 'max_rate': 2}}}]}",
            "it has none" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {'clip': 'x', 'match_speed': 'speed', 'min_rate': 3, "
            "'max_rate': 2}}}]}", "above max_rate" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {'is_looped': true}}}]}", "no clip to loop" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {'clip': 'x', 'look_at': 2}}}]}", "look_at is 2" },
        // Blends.
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {'blend': {'parameter': 'speed', 'points': [{'clip': "
            "'x', 'at': 0}]}}}}]}", "a blend has 2 to 8" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {'blend': {'parameter': 'speed', 'points': [{'clip': "
            "'x', 'at': 1}, {'clip': 'y', 'at': 1}]}}}}]}", "must rise" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {'blend': {'parameter': 'speed', 'points': [{'clip': "
            "'x', 'at': 1}, {'clip': 'y'}]}}}}]}", "needs \"at\"" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {'blend': {'points': []}}}}]}", "needs \"parameter\"" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {'blend': {'parameter': 'speed', 'points': [{'clip': "
            "'x', 'at': 0, 'weight': 1}, {'clip': 'y', 'at': 1}]}}}}]}", "weight" },
        // Events.
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {'events': [{'name': 'e', 'phase': 0}]}}}]}",
            "empty state plays nothing" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {'clip': 'x', 'events': [{'name': 'e'}]}}}]}",
            "either \"time\"" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {'clip': 'x', 'events': [{'name': 'e', 'time': 0, "
            "'phase': 0}]}}}]}", "either \"time\"" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {'clip': 'x', 'events': [{'name': 'e', 'phase': 1}]}}}]}",
            "must be below 1" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {'blend': {'parameter': 'speed', 'points': [{'clip': "
            "'x', 'at': 0}, {'clip': 'y', 'at': 1}]}, 'events': [{'name': 'e', 'time': 0.1}]}}}]}", "needs a one-clip state" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {'clip': 'x', 'events': [{'name': 'e', 'time': 0, "
            "'grip': 'g'}]}}}]}", "it needs \"attach\"" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {'clip': 'x', 'events': [{'name': 'e', 'time': 0, "
            "'attach': 'Hand'}]}}}]}", "not a socket's name" },
        { "{'parameters': {'t': {'type': 'trigger'}}, 'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {'clip': 'x', "
            "'events': [{'name': 'e', 'time': 0, 'set': {'t': 1}}]}}}]}", "not a custom float or bool" },
        { "{'parameters': {'p': {'type': 'float', 'max': 1}}, 'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': "
            "{'clip': 'x', 'events': [{'name': 'e', 'time': 0, 'set': {'p': 2}}]}}}]}", "the parameter takes" },
        { "{'parameters': {'p': {'type': 'bool'}}, 'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {'clip': 'x', "
            "'events': [{'name': 'e', 'time': 0, 'set': {'p': 2}}]}}}]}", "must be true or false" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {'clip': 'x', 'events': [{'name': 'E', 'time': 0}]}}}]}",
            "not an event's name" },
        // Transitions and conditions.
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {}}, 'transitions': [{'to': 'a'}]}]}",
            "needs \"from\"" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {}, 'b': {}}, 'transitions': [{'from': 'a', 'to': 'c'}]}]}",
            "not a state of" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {}, 'b': {}}, 'transitions': [{'from': ['a', 'a'], "
            "'to': 'b'}]}]}", "already holds" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {}, 'b': {}}, 'transitions': [{'from': ['any'], "
            "'to': 'b'}]}]}", "not a state of" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {}, 'b': {}}, 'transitions': [{'from': 'a', 'to': 'b', "
            "'exit_time': 1}]}]}", "is empty" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {}}, 'transitions': [{'from': 'a', 'to': 'a'}]}]}",
            "to itself" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {}, 'b': {}}, 'transitions': [{'from': 'a', 'to': 'b', "
            "'when': [{'parameter': 'speed', 'event': 'jumped', 'above': 1}]}]}]}", "tests one thing" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {}, 'b': {}}, 'transitions': [{'from': 'a', 'to': 'b', "
            "'when': [{'parameter': 'speed', 'is': true}]}]}]}", "\"is\" tests a bool" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {}, 'b': {}}, 'transitions': [{'from': 'a', 'to': 'b', "
            "'when': [{'parameter': 'speed', 'above': 1, 'below': 2}]}]}]}", "exactly one test" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {}, 'b': {}}, 'transitions': [{'from': 'a', 'to': 'b', "
            "'when': [{'parameter': 'speed'}]}]}]}", "exactly one test" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {}, 'b': {}}, 'transitions': [{'from': 'a', 'to': 'b', "
            "'when': [{'event': 'sneezed'}]}]}]}", "neither a built-in event" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {}, 'b': {}}, 'transitions': [{'from': 'a', 'to': 'b', "
            "'when': [{'socket': 'hand'}]}]}]}", "needs \"is_holding\"" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {}, 'b': {}}, 'transitions': [{'from': 'a', 'to': 'b', "
            "'when': [{'parameter': 'speed', 'above': 1, 'unless': 2}]}]}]}", "unless" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {}, 'b': {}}, 'transitions': [{'from': 'a', 'to': 'b', "
            "'fade_seconds': 61}]}]}", "fade_seconds is 61" },
        // Parameters.
        { "{'parameters': {'speed': {'type': 'float'}}, 'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {}}}]}",
            "a name of its own" },
        { "{'parameters': {'landed': {'type': 'trigger'}}, 'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {}}}]}",
            "a name of its own" },
        { "{'parameters': {'p': {'type': 'int'}}, 'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {}}}]}",
            "\"float\", \"bool\" or \"trigger\"" },
        { "{'parameters': {'p': {'type': 'float', 'min': 2, 'max': 1}}, 'layers': [{'name': 'body', 'entry': 'a', 'states': "
            "{'a': {}}}]}", "is above max" },
        { "{'parameters': {'p': {'type': 'float', 'max': 1, 'default': 3}}, 'layers': [{'name': 'body', 'entry': 'a', "
            "'states': {'a': {}}}]}", "default is 3" },
        { "{'parameters': {'p': {'type': 'bool', 'min': 0}}, 'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {}}}]}",
            "min" },
        { "{'parameters': {'p': {'type': 'trigger', 'default': 1}}, 'layers': [{'name': 'body', 'entry': 'a', 'states': "
            "{'a': {}}}]}", "default" },
        // Look-at.
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {}}}], 'look_at': {}}", "needs \"bones\"" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {}}}], 'look_at': {'bones': [{'bone': 'neck', "
            "'share': 0.7}, {'bone': 'head', 'share': 0.7}]}}", "add up to" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {}}}], 'look_at': {'bones': [{'bone': 'head', "
            "'share': 0}]}}", "share is 0" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {}}}], 'look_at': {'bones': [{'bone': 'head'}], "
            "'max_yaw': 0}}", "max_yaw is 0" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {}}}], 'look_at': {'bones': [{'bone': 'head'}], "
            "'max_pitch_up': 91}}", "max_pitch_up is 91" },
        // Actions.
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {}}}], 'actions': {'go': {'states': ['a']}}}",
            "needs \"layer\"" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {}}}], 'actions': {'go': {'layer': 'legs', 'states': "
            "['a']}}}", "not a layer" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {}}}], 'actions': {'go': {'layer': 'body', 'states': "
            "[]}}}", "an action has 1 to" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {}}}], 'actions': {'go': {'layer': 'body', 'states': "
            "['b']}}}", "not a state of" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {}}}], 'actions': {'go': {'layer': 'body', 'states': "
            "['a', 'a']}}}", "already holds" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {}}}], 'actions': {'go': {'layer': 'body', 'states': "
            "['a'], 'movement': 'frozen'}}}", "\"free\", \"no_moving\"" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {}}}], 'actions': {'go': {'layer': 'body', 'states': "
            "['a'], 'target': 'optional', 'anchor': {'socket': 'seat'}}}}", "is \"required\"" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {}}}], 'actions': {'go': {'layer': 'body', 'states': "
            "['a'], 'anchor': {'max_distance': 1}}}}", "needs \"socket\"" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {}}}], 'actions': {'go': {'layer': 'body', 'states': "
            "['a'], 'anchor': {'socket': 'seat', 'max_distance': 0}}}}", "max_distance is 0" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {'clip': 'x', 'events': [{'name': 'e', 'time': 0, "
            "'attach': 'hand'}]}}}], 'actions': {'go': {'layer': 'body', 'states': ['a'], 'target': 'none'}}}",
            "cannot be \"none\"" },
        { "{'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {}}}], 'actions': {'Go': {'layer': 'body', 'states': "
            "['a']}}}", "not a valid action name" },
    };
    GraphJson Json;
    if (!GraphJson_Construct(run, &Json))
    {
        return;
    }
    for (size_t Index = 0U; Index < (sizeof(Cases) / sizeof(Cases[0])); Index++)
    {
        CheckRefused(run, &Json, Cases[Index].Json, Cases[Index].Fragment);
    }
    GraphJson_Deconstruct(run, &Json);
}

static void TheLimitsHold(TestRun* run)
{
    GraphJson Json;
    static char Text[GRAPH_TEXT_CAPACITY];
    if (!GraphJson_Construct(run, &Json))
    {
        return;
    }
    // ANIMATION_GRAPH_MAX_STATES states pass, one more is refused.
    for (uint32_t Extra = 0U; Extra < 2U; Extra++)
    {
        size_t Length = (size_t)snprintf(Text, sizeof(Text), "{'layers': [{'name': 'body', 'entry': 's0', 'states': {");
        for (uint32_t State = 0U; State < (ANIMATION_GRAPH_MAX_STATES + Extra); State++)
        {
            Length += (size_t)snprintf(Text + Length, sizeof(Text) - Length, "%s's%u': {}", (State > 0U) ? ", " : "",
                (unsigned)State);
        }
        snprintf(Text + Length, sizeof(Text) - Length, "}}]}");
        if (Extra == 0U)
        {
            AnimationGraph* Graph = ReadGraph(run, &Json, Text);
            TEST_CHECK(run, (Graph != NULL) && (AnimationGraph_GetStateCount(Graph) == ANIMATION_GRAPH_MAX_STATES));
            AnimationGraph_Destroy(Graph);
        }
        else
        {
            CheckRefused(run, &Json, Text, "more than 64 states");
        }
    }
    // Triggers: at most ANIMATION_GRAPH_MAX_TRIGGERS.
    size_t Length = (size_t)snprintf(Text, sizeof(Text), "{'parameters': {");
    for (uint32_t Trigger = 0U; Trigger <= ANIMATION_GRAPH_MAX_TRIGGERS; Trigger++)
    {
        Length += (size_t)snprintf(Text + Length, sizeof(Text) - Length, "%s't%u': {'type': 'trigger'}",
            (Trigger > 0U) ? ", " : "", (unsigned)Trigger);
    }
    snprintf(Text + Length, sizeof(Text) - Length, "}, 'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {}}}]}");
    CheckRefused(run, &Json, Text, "more than 32 triggers");
    // Parameters: at most ANIMATION_GRAPH_MAX_PARAMETERS.
    Length = (size_t)snprintf(Text, sizeof(Text), "{'parameters': {");
    for (uint32_t Parameter = 0U; Parameter <= ANIMATION_GRAPH_MAX_PARAMETERS; Parameter++)
    {
        Length += (size_t)snprintf(Text + Length, sizeof(Text) - Length, "%s'p%u': {'type': 'bool'}",
            (Parameter > 0U) ? ", " : "", (unsigned)Parameter);
    }
    snprintf(Text + Length, sizeof(Text) - Length, "}, 'layers': [{'name': 'body', 'entry': 'a', 'states': {'a': {}}}]}");
    CheckRefused(run, &Json, Text, "more than 64 parameters");
    // Layers: at most ANIMATION_GRAPH_MAX_LAYERS.
    Length = (size_t)snprintf(Text, sizeof(Text), "{'layers': [");
    for (uint32_t Layer = 0U; Layer <= ANIMATION_GRAPH_MAX_LAYERS; Layer++)
    {
        Length += (size_t)snprintf(Text + Length, sizeof(Text) - Length,
            "%s{'name': 'l%u', 'entry': 'a', 'states': {'a': {}}}", (Layer > 0U) ? ", " : "", (unsigned)Layer);
    }
    snprintf(Text + Length, sizeof(Text) - Length, "]}");
    CheckRefused(run, &Json, Text, "1 to 8");
    // Transitions: a list of states counts once per state against ANIMATION_GRAPH_MAX_TRANSITIONS.
    Length = (size_t)snprintf(Text, sizeof(Text), "{'layers': [{'name': 'body', 'entry': 's0', 'states': {");
    for (uint32_t State = 0U; State < ANIMATION_GRAPH_MAX_STATES; State++)
    {
        Length += (size_t)snprintf(Text + Length, sizeof(Text) - Length, "%s's%u': {}", (State > 0U) ? ", " : "",
            (unsigned)State);
    }
    Length += (size_t)snprintf(Text + Length, sizeof(Text) - Length, "}, 'transitions': [");
    for (uint32_t Transition = 0U; Transition < 5U; Transition++)
    {
        Length += (size_t)snprintf(Text + Length, sizeof(Text) - Length, "%s{'from': [", (Transition > 0U) ? ", " : "");
        for (uint32_t State = 1U; State < ANIMATION_GRAPH_MAX_STATES; State++)
        {
            Length += (size_t)snprintf(Text + Length, sizeof(Text) - Length, "%s's%u'", (State > 1U) ? ", " : "",
                (unsigned)State);
        }
        Length += (size_t)snprintf(Text + Length, sizeof(Text) - Length, "], 'to': 's0'}");
    }
    snprintf(Text + Length, sizeof(Text) - Length, "]}]}");
    CheckRefused(run, &Json, Text, "more than 256 transitions");
    GraphJson_Deconstruct(run, &Json);
}


// Public functions.
void AnimationGraphTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "a_full_graph_reads_every_part", AFullGraphReadsEveryPart },
        { "defaults_fill_what_a_graph_leaves_out", DefaultsFillWhatAGraphLeavesOut },
        { "the_block_copies_as_bytes", TheBlockCopiesAsBytes },
        { "the_asset_type_loads_a_copy_of_the_definition", TheAssetTypeLoadsACopyOfTheDefinition },
        { "bad_definitions_are_refused_with_their_reason", BadDefinitionsAreRefusedWithTheirReason },
        { "the_limits_hold", TheLimitsHold },
    };
    TestRun_RunSuite(run, "AnimationGraph", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
