#include <math.h>
#include "AnimationGraphPlayer.h"
#include "AnimationLibrary.h"
#include "raylib/raymath.h"
#include "wr/WRMemory.h"

/*
 * The graph player (AnimationGraphPlayer.h): bindings, playback, transitions, timed events, the layered pose and
 * look-at. Pure: nothing here allocates; every array lives in storage the caller gives.
 */


// Macros.
/** Every array of a storage block starts at a multiple of this. */
#define STORAGE_ALIGNMENT ((size_t)8)
/** How long a state whose clips no library has runs, in seconds, as a clip that does not loop would: exit times pass. */
#define MISSING_CLIP_SECONDS 1.0
/** Degrees to radians. */
#define DEGREES_TO_RADIANS 0.017453292519943295f


// Static functions: storage.
static size_t AlignUp(size_t value)
{
    return (value + (STORAGE_ALIGNMENT - 1U)) & ~(STORAGE_ALIGNMENT - 1U);
}

/* How many clips a state plays: one, a blend's points, or none. */
static uint32_t GetStateClipCount(const AnimationGraphState* state)
{
    switch (state->_kind)
    {
        case AnimationGraphStateKind_Empty: return 0U;
        case AnimationGraphStateKind_Clip: return 1U;
        case AnimationGraphStateKind_Blend: return state->_pointCount;
    }
    return 0U;
}

static uint32_t GetTotalClipCount(const AnimationGraph* graph)
{
    uint32_t Count = 0U;
    for (uint32_t Index = 0U; Index < AnimationGraph_GetStateCount(graph); Index++)
    {
        Count += GetStateClipCount(AnimationGraph_GetState(graph, Index));
    }
    return Count;
}


// Static functions: binding.
/* Finds a clip by name in the libraries, in order, with its natural speed on the skeleton. */
static void BindClip(AnimationGraphBinding* self, const unsigned char* name, AnimationGraphBoundClip* outClip)
{
    Memory_Zero(outClip, sizeof(*outClip));
    for (uint32_t Library = 0U; Library < self->LibraryCount; Library++)
    {
        const SkeletalBinding* Binding = &self->Libraries[Library];
        uint32_t Index = 0U;
        if (!AnimationLibrary_TryFindClip(Binding->Library, name, &Index))
        {
            continue;
        }
        const AnimationClip* Clip = AnimationLibrary_GetClip(Binding->Library, Index);
        outClip->IsFound = true;
        outClip->Clip = (SkeletalClipRef)
        {
            .Library = Library, .Clip = Index, .Duration = AnimationClip_GetDuration(Clip),
            .IsLooped = AnimationClip_IsLooped(Clip), .Speed = AnimationClip_GetSpeed(Clip)
        };
        float Distance = 0.0f;
        if (AnimationClip_TryGetDistance(Clip, &Distance) && (outClip->Clip.Duration > 0.0f))
        {
            outClip->NaturalSpeed = (Distance * Binding->HeightRatio * outClip->Clip.Speed) / outClip->Clip.Duration;
        }
        return;
    }
    self->MissingClipCount++;
    if (self->FirstMissingClip == NULL)
    {
        self->FirstMissingClip = name;
    }
}

static void NoteMissingBone(AnimationGraphBinding* self, const unsigned char* name)
{
    self->MissingBoneCount++;
    if (self->FirstMissingBone == NULL)
    {
        self->FirstMissingBone = name;
    }
}

/* One layer's weight per bone: its weight on the bones of its mask and their subtrees (every bone without a mask). */
static void BuildBoneWeights(AnimationGraphBinding* self, const AnimationGraphLayer* layer, float* outWeights)
{
    const GameSkeleton* Skeleton = self->Skeleton;
    uint32_t BoneCount = GameSkeleton_GetBoneCount(Skeleton);
    if (layer->_maskBoneCount == 0U)
    {
        for (uint32_t Bone = 0U; Bone < BoneCount; Bone++)
        {
            outWeights[Bone] = layer->_weight;
        }
        return;
    }
    for (uint32_t Bone = 0U; Bone < BoneCount; Bone++)
    {
        outWeights[Bone] = 0.0f;
    }
    for (uint32_t Index = 0U; Index < layer->_maskBoneCount; Index++)
    {
        const unsigned char* Name = AnimationGraph_GetMaskBone(self->Graph, layer, Index);
        uint32_t Bone = 0U;
        if (GameSkeleton_TryFindBone(Skeleton, Name, &Bone))
        {
            outWeights[Bone] = layer->_weight;
            continue;
        }
        NoteMissingBone(self, Name);
    }
    // The evaluation order puts every parent before its children: a bone under a masked one takes its weight.
    const uint16_t* Order = GameSkeleton_GetOrder(Skeleton);
    for (uint32_t Step = 0U; Step < BoneCount; Step++)
    {
        uint32_t Bone = Order[Step];
        int32_t Parent = GameSkeleton_GetBone(Skeleton, Bone)->_parent;
        if ((Parent >= 0) && (outWeights[Parent] > 0.0f))
        {
            outWeights[Bone] = layer->_weight;
        }
    }
}

static void BuildLookAt(AnimationGraphBinding* self)
{
    const AnimationGraphLookAt* LookAt = AnimationGraph_GetLookAt(self->Graph);
    self->LookBoneCount = 0U;
    if (!LookAt->_isEnabled)
    {
        return;
    }
    for (uint32_t Index = 0U; Index < LookAt->_boneCount; Index++)
    {
        const AnimationGraphLookBone* Bone = AnimationGraph_GetLookBone(self->Graph, Index);
        const unsigned char* Name = AnimationGraph_GetString(self->Graph, Bone->_bone);
        uint32_t Found = 0U;
        if (!GameSkeleton_TryFindBone(self->Skeleton, Name, &Found))
        {
            NoteMissingBone(self, Name);
            continue;
        }
        self->LookBones[self->LookBoneCount] = Found;
        self->LookShares[self->LookBoneCount] = Bone->_share;
        self->LookBoneCount++;
    }
}


// Static functions: the instance's parts.
static const AnimationGraphLayer* GetLayer(const AnimationGraphInstance* self, uint32_t layer)
{
    return AnimationGraph_GetLayer(self->Graph, layer);
}

static const AnimationGraphState* GetLayerState(const AnimationGraphInstance* self, uint32_t layer, uint32_t state)
{
    return AnimationGraph_GetLayerState(self->Graph, GetLayer(self, layer), state);
}

static const AnimationGraphBoundClip* GetStateClips(const AnimationGraphBinding* binding, const AnimationGraphLayer* layer,
    uint32_t state)
{
    return binding->Clips + binding->StateClips[layer->_firstState + state];
}

static AnimationGraphEntry* FindEntry(AnimationGraphLayerPlayback* playback, uint32_t state)
{
    for (uint32_t Index = 0U; Index < playback->EntryCount; Index++)
    {
        if (playback->Entries[Index].State == state)
        {
            return &playback->Entries[Index];
        }
    }
    return NULL;
}

static void StartEntryOver(AnimationGraphEntry* entry)
{
    entry->Time = 0.0;
    entry->Cycles = 0.0;
    entry->HasEnded = false;
    entry->HasAdvanced = false;
}

static void StartFade(AnimationGraphEntry* entry, float target, float seconds)
{
    entry->StartWeight = entry->Weight;
    entry->TargetWeight = target;
    entry->FadeSeconds = seconds;
    entry->FadeElapsed = 0.0f;
    if (!(seconds > 0.0f))
    {
        entry->Weight = target;
        entry->FadeSeconds = 0.0f;
    }
}

static void AdvanceFade(AnimationGraphEntry* entry, float seconds)
{
    if (entry->FadeElapsed >= entry->FadeSeconds)
    {
        entry->Weight = entry->TargetWeight;
        return;
    }
    entry->FadeElapsed += seconds;
    float Progress = (entry->FadeElapsed >= entry->FadeSeconds) ? 1.0f : (entry->FadeElapsed / entry->FadeSeconds);
    entry->Weight = entry->StartWeight + ((entry->TargetWeight - entry->StartWeight) * Progress);
}

static void RemoveFadedOut(AnimationGraphLayerPlayback* playback)
{
    for (uint32_t Index = playback->EntryCount; Index > 0U; Index--)
    {
        const AnimationGraphEntry* Entry = &playback->Entries[Index - 1U];
        if ((Entry->Weight <= 0.0f) && (Entry->TargetWeight <= 0.0f) && (Entry->State != playback->Current))
        {
            for (uint32_t Next = Index; Next < playback->EntryCount; Next++)
            {
                playback->Entries[Next - 1U] = playback->Entries[Next];
            }
            playback->EntryCount--;
        }
    }
}


// Static functions: conditions and transitions.
static bool IsEventFired(const AnimationGraphInstance* self, uint32_t index)
{
    return (index < 64U) && (((self->Events >> index) & 1U) != 0U);
}

static bool DoesConditionHold(const AnimationGraphInstance* self, const AnimationGraphCondition* condition,
    const AnimationGraphContext* context)
{
    switch (condition->_kind)
    {
        case AnimationGraphConditionKind_Event:
            return IsEventFired(self, condition->_subject);
        case AnimationGraphConditionKind_Socket:
        {
            bool IsHolding = (context != NULL) && (context->IsSocketHolding != NULL)
                && context->IsSocketHolding(context->User, AnimationGraph_GetString(self->Graph, condition->_subject));
            return IsHolding == (condition->_value > 0.5f);
        }
        case AnimationGraphConditionKind_Parameter:
        {
            float Value = AnimationGraphInstance_GetParameter(self, condition->_subject);
            switch (condition->_compare)
            {
                case AnimationGraphCompare_Above: return Value > condition->_value;
                case AnimationGraphCompare_Below: return Value < condition->_value;
                case AnimationGraphCompare_AtLeast: return Value >= condition->_value;
                case AnimationGraphCompare_AtMost: return Value <= condition->_value;
                case AnimationGraphCompare_Is: return (Value > 0.5f) == (condition->_value > 0.5f);
            }
            return false;
        }
    }
    return false;
}

static bool CanTake(const AnimationGraphInstance* self, uint32_t layer, const AnimationGraphTransition* transition,
    const AnimationGraphContext* context)
{
    AnimationGraphLayerPlayback* Playback = &self->Layers[layer];
    if ((transition->_from != ANIMATION_GRAPH_NONE) && (transition->_from != Playback->Current))
    {
        return false;
    }
    if ((transition->_to == Playback->Current) && !transition->_isRestarted)
    {
        return false;
    }
    if (transition->_hasExitTime)
    {
        const AnimationGraphEntry* Current = FindEntry(Playback, Playback->Current);
        if ((Current == NULL) || (Current->Cycles < (double)transition->_exitTime))
        {
            return false;
        }
    }
    for (uint32_t Index = 0U; Index < transition->_conditionCount; Index++)
    {
        if (!DoesConditionHold(self, AnimationGraph_GetCondition(self->Graph, transition->_firstCondition + Index),
            context))
        {
            return false;
        }
    }
    return true;
}

static void TakeTransitions(AnimationGraphInstance* self, uint32_t layer, const AnimationGraphContext* context)
{
    const AnimationGraphLayer* Layer = GetLayer(self, layer);
    for (uint32_t Index = 0U; Index < Layer->_transitionCount; Index++)
    {
        const AnimationGraphTransition* Transition = AnimationGraph_GetTransition(self->Graph,
            Layer->_firstTransition + Index);
        if (CanTake(self, layer, Transition, context))
        {
            AnimationGraphInstance_EnterState(self, layer, Transition->_to, Transition->_fadeSeconds,
                Transition->_isRestarted);
            return;
        }
    }
}


// Static functions: playing a state.
/* Whether the playback, moving from @p start by @p advance over a cycle of @p length, passes @p position (at or after
 * the start when @p includesStart, strictly after it otherwise; a looping cycle comes round again). */
static bool IsPassed(double position, double start, double advance, double length, bool includesStart, bool isLooped)
{
    if (!isLooped || !(length > 0.0))
    {
        double End = start + advance;
        return (includesStart ? (position >= start) : (position > start)) && (position <= End);
    }
    double Next = position;
    if (Next < start)
    {
        Next += ceil((start - Next) / length) * length;
    }
    if (!includesStart && (Next <= start))
    {
        Next += length;
    }
    return Next <= (start + advance);
}

/* The rate a state plays at: its speed, times its speed parameter, times a matched speed's ratio within its limits. */
static double GetStateRate(const AnimationGraphInstance* self, const AnimationGraphBinding* binding,
    const AnimationGraphLayer* layer, uint32_t stateIndex, const AnimationGraphState* state)
{
    double Rate = (double)state->_speed;
    if (state->_speedParameter != ANIMATION_GRAPH_NONE)
    {
        Rate *= (double)AnimationGraphInstance_GetParameter(self, state->_speedParameter);
    }
    if (state->_matchParameter != ANIMATION_GRAPH_NONE)
    {
        const AnimationGraphBoundClip* Clips = GetStateClips(binding, layer, stateIndex);
        double Natural = 0.0;
        if (state->_kind == AnimationGraphStateKind_Clip)
        {
            Natural = (double)Clips[0].NaturalSpeed;
        }
        else if (state->_kind == AnimationGraphStateKind_Blend)
        {
            float Weights[ANIMATION_GRAPH_MAX_BLEND_POINTS];
            AnimationGraphPlayer_GetBlendWeights(self->Graph, state, AnimationGraphInstance_GetParameter(self,
                state->_blendParameter), Weights);
            for (uint32_t Point = 0U; Point < state->_pointCount; Point++)
            {
                Natural += (double)Weights[Point] * (double)Clips[Point].NaturalSpeed;
            }
        }
        if (Natural > 0.0)
        {
            double Ratio = (double)AnimationGraphInstance_GetParameter(self, state->_matchParameter) / Natural;
            Ratio = (Ratio < (double)state->_minRate) ? (double)state->_minRate
                : ((Ratio > (double)state->_maxRate) ? (double)state->_maxRate : Ratio);
            Rate *= Ratio;
        }
    }
    return (isfinite(Rate) && (Rate > 0.0)) ? Rate : 0.0;
}

/* A one-clip state's clip: its seconds, whether it loops, its own speed (a missing clip runs as one second once). */
static void GetClipPlayback(const AnimationGraphState* state, const AnimationGraphBoundClip* clip, double* outDuration,
    bool* outIsLooped, double* outSpeed)
{
    if (!clip->IsFound)
    {
        *outDuration = MISSING_CLIP_SECONDS;
        *outIsLooped = false;
        *outSpeed = 1.0;
        return;
    }
    *outDuration = (double)clip->Clip.Duration;
    *outIsLooped = state->_overridesIsLooped ? state->_isLooped : clip->Clip.IsLooped;
    *outSpeed = (double)clip->Clip.Speed;
}

/* A blend's cycle: its clips' cycle seconds (at their own speeds) by their weights; a second when none has length. */
static double GetBlendCycleSeconds(const AnimationGraphInstance* self, const AnimationGraphState* state,
    const AnimationGraphBoundClip* clips)
{
    float Weights[ANIMATION_GRAPH_MAX_BLEND_POINTS];
    AnimationGraphPlayer_GetBlendWeights(self->Graph, state, AnimationGraphInstance_GetParameter(self,
        state->_blendParameter), Weights);
    double Seconds = 0.0;
    for (uint32_t Point = 0U; Point < state->_pointCount; Point++)
    {
        double Duration = clips[Point].IsFound ? (double)clips[Point].Clip.Duration : MISSING_CLIP_SECONDS;
        double Speed = clips[Point].IsFound ? (double)clips[Point].Clip.Speed : 1.0;
        if ((Duration > 0.0) && (Speed > 0.0))
        {
            Seconds += (double)Weights[Point] * (Duration / Speed);
        }
    }
    return (Seconds > 0.0) ? Seconds : MISSING_CLIP_SECONDS;
}

/* Fires the events of the state the layer is in that its playback passed: "set" effects here, then the context. */
static void FireStateEvents(AnimationGraphInstance* self, uint32_t layer, const AnimationGraphState* state,
    uint32_t stateIndex, double start, double advance, double length, bool includesStart, bool isLooped,
    const AnimationGraphContext* context)
{
    for (uint32_t Index = 0U; Index < state->_eventCount; Index++)
    {
        const AnimationGraphEventRecord* Event = AnimationGraph_GetEvent(self->Graph, state->_firstEvent + Index);
        // A phase is a share of the cycle; a time is seconds (one-clip states only, whose cycle is in seconds).
        double Position = Event->_isPhase ? ((double)Event->_at * length) : (double)Event->_at;
        if (!IsPassed(Position, start, advance, length, includesStart, isLooped))
        {
            continue;
        }
        for (uint32_t Set = 0U; Set < Event->_setCount; Set++)
        {
            const AnimationGraphParameterSet* Parameter = AnimationGraph_GetParameterSet(self->Graph,
                Event->_firstSet + Set);
            AnimationGraphInstance_SetParameter(self, Parameter->_parameter, Parameter->_value);
        }
        if ((context != NULL) && (context->OnEvent != NULL))
        {
            context->OnEvent(context->User, layer, stateIndex, Event);
        }
    }
}

/* Advances one playing state by the tick's seconds; the layer's current state fires its events. */
static void AdvanceEntry(AnimationGraphInstance* self, const AnimationGraphBinding* binding, uint32_t layer,
    AnimationGraphEntry* entry, double seconds, const AnimationGraphContext* context)
{
    const AnimationGraphLayer* Layer = GetLayer(self, layer);
    const AnimationGraphState* State = AnimationGraph_GetLayerState(self->Graph, Layer, entry->State);
    bool IsCurrent = entry->State == self->Layers[layer].Current;
    bool IncludesStart = !entry->HasAdvanced;
    entry->HasAdvanced = true;
    if (State->_kind == AnimationGraphStateKind_Empty)
    {
        return;
    }
    const AnimationGraphBoundClip* Clips = GetStateClips(binding, Layer, entry->State);
    double Rate = GetStateRate(self, binding, Layer, entry->State, State);
    double Start = entry->Time;
    double Length = 1.0;
    double Advance = 0.0;
    bool IsLooped = true;
    if (State->_kind == AnimationGraphStateKind_Clip)
    {
        double Speed = 1.0;
        GetClipPlayback(State, &Clips[0], &Length, &IsLooped, &Speed);
        Advance = entry->HasEnded ? 0.0 : (seconds * Rate * Speed);
        if (!(Length > 0.0))
        {
            // A clip of one pose: it has played its cycle at once.
            entry->Time = 0.0;
            entry->Cycles = IsLooped ? (entry->Cycles + Advance) : 1.0;
            entry->HasEnded = !IsLooped;
            Length = 0.0;
        }
        else if (IsLooped)
        {
            double Time = entry->Time + Advance;
            entry->Time = Time - (floor(Time / Length) * Length);
            entry->Cycles += Advance / Length;
        }
        else
        {
            double Time = entry->Time + Advance;
            entry->Time = (Time >= Length) ? Length : Time;
            entry->Cycles = entry->Time / Length;
            entry->HasEnded = entry->Time >= Length;
        }
    }
    else
    {
        // A blend plays by phase: one cycle a second at rate 1 over its clips' weighted cycle.
        IsLooped = State->_overridesIsLooped ? State->_isLooped : true;
        Advance = entry->HasEnded ? 0.0 : ((seconds * Rate) / GetBlendCycleSeconds(self, State, Clips));
        double Phase = entry->Time + Advance;
        if (IsLooped)
        {
            entry->Time = Phase - floor(Phase);
            entry->Cycles += Advance;
        }
        else
        {
            entry->Time = (Phase >= 1.0) ? 1.0 : Phase;
            entry->Cycles = entry->Time;
            entry->HasEnded = entry->Time >= 1.0;
        }
    }
    if (IsCurrent && (State->_eventCount > 0U))
    {
        if (Length > 0.0)
        {
            FireStateEvents(self, layer, State, entry->State, Start, Advance, Length, IncludesStart, IsLooped, context);
        }
        else if (IncludesStart)
        {
            // A clip of one pose plays its whole cycle at once: every event of it fires as the state starts, once (its
            // time, kept at 0, would otherwise pass the same events again every tick).
            FireStateEvents(self, layer, State, entry->State, 0.0, (double)ANIMATION_GRAPH_MAX_SECONDS, 0.0, true, false,
                context);
        }
    }
}

/* Look-at's turn eases toward the look parameters within the graph's limits; its weight is the playing states'. */
static void UpdateLookAt(AnimationGraphInstance* self, double seconds)
{
    const AnimationGraphLookAt* LookAt = AnimationGraph_GetLookAt(self->Graph);
    if (!LookAt->_isEnabled)
    {
        self->LookWeight = 0.0f;
        return;
    }
    float TargetYaw = Clamp(AnimationGraphInstance_GetParameter(self, (uint32_t)AnimationGraphBuiltIn_LookYaw)
        * DEGREES_TO_RADIANS, -LookAt->_maxYaw, LookAt->_maxYaw);
    float TargetPitch = Clamp(AnimationGraphInstance_GetParameter(self, (uint32_t)AnimationGraphBuiltIn_LookPitch)
        * DEGREES_TO_RADIANS, -LookAt->_maxPitchDown, LookAt->_maxPitchUp);
    if (isfinite(seconds) && (seconds > 0.0))
    {
        float Step = (float)((double)LookAt->_turnSpeed * seconds);
        float YawGap = TargetYaw - self->LookYaw;
        float PitchGap = TargetPitch - self->LookPitch;
        self->LookYaw += (fabsf(YawGap) <= Step) ? YawGap : ((YawGap > 0.0f) ? Step : -Step);
        self->LookPitch += (fabsf(PitchGap) <= Step) ? PitchGap : ((PitchGap > 0.0f) ? Step : -Step);
    }

    // The first layer's states set the weight (what they leave counts fully); each further layer moves it toward its
    // own states' weight by how much it plays.
    float Weight = 1.0f;
    for (uint32_t Layer = 0U; Layer < AnimationGraph_GetLayerCount(self->Graph); Layer++)
    {
        const AnimationGraphLayerPlayback* Playback = &self->Layers[Layer];
        float Sum = 0.0f;
        float Share = 0.0f;
        for (uint32_t Index = 0U; Index < Playback->EntryCount; Index++)
        {
            const AnimationGraphEntry* Entry = &Playback->Entries[Index];
            const AnimationGraphState* State = GetLayerState(self, Layer, Entry->State);
            if ((Layer > 0U) && (State->_kind == AnimationGraphStateKind_Empty))
            {
                continue;
            }
            Sum += Entry->Weight * State->_lookAtWeight;
            Share += Entry->Weight;
        }
        if (Layer == 0U)
        {
            Weight = Sum + ((Share < 1.0f) ? (1.0f - Share) : 0.0f);
            continue;
        }
        float Coverage = ((Share < 1.0f) ? Share : 1.0f) * GetLayer(self, Layer)->_weight;
        if (Share > 0.0f)
        {
            Weight += ((Sum / Share) - Weight) * Coverage;
        }
    }
    self->LookWeight = Clamp(Weight, 0.0f, 1.0f);
}


// Static functions: posing.
/* Adds one layer's playing states to a blend: each clip's sample by its state's weight times its weight in the state.
 * Returns the total weight added. */
static float AddLayerToBlend(const AnimationGraphInstance* self, const AnimationGraphBinding* binding, uint32_t layer,
    Transform* scratch, SkeletalBlendBone* blend)
{
    const AnimationGraphLayer* Layer = GetLayer(self, layer);
    const AnimationGraphLayerPlayback* Playback = &self->Layers[layer];
    uint32_t BoneCount = GameSkeleton_GetBoneCount(binding->Skeleton);
    float Total = 0.0f;
    for (uint32_t Index = 0U; Index < Playback->EntryCount; Index++)
    {
        const AnimationGraphEntry* Entry = &Playback->Entries[Index];
        const AnimationGraphState* State = AnimationGraph_GetLayerState(self->Graph, Layer, Entry->State);
        if (!(Entry->Weight > 0.0f) || (State->_kind == AnimationGraphStateKind_Empty))
        {
            continue;
        }
        const AnimationGraphBoundClip* Clips = GetStateClips(binding, Layer, Entry->State);
        if (State->_kind == AnimationGraphStateKind_Clip)
        {
            if (!Clips[0].IsFound)
            {
                continue;
            }
            SkeletalBinding_SampleClip(&binding->Libraries[Clips[0].Clip.Library], Clips[0].Clip.Clip, (float)Entry->Time,
                scratch);
            SkeletalPose_AddToBlend(blend, scratch, BoneCount, Entry->Weight);
            Total += Entry->Weight;
            continue;
        }
        float Weights[ANIMATION_GRAPH_MAX_BLEND_POINTS];
        AnimationGraphPlayer_GetBlendWeights(self->Graph, State, AnimationGraphInstance_GetParameter(self,
            State->_blendParameter), Weights);
        for (uint32_t Point = 0U; Point < State->_pointCount; Point++)
        {
            float Weight = Entry->Weight * Weights[Point];
            if (!(Weight > 0.0f) || !Clips[Point].IsFound)
            {
                continue;
            }
            // In step: every clip of the blend at the same share of its own cycle.
            float Time = (float)(Entry->Time * (double)Clips[Point].Clip.Duration);
            SkeletalBinding_SampleClip(&binding->Libraries[Clips[Point].Clip.Library], Clips[Point].Clip.Clip, Time,
                scratch);
            SkeletalPose_AddToBlend(blend, scratch, BoneCount, Weight);
            Total += Weight;
        }
    }
    return Total;
}

/* Whether the playback is one found clip at full weight on a lone, unmasked, fully weighted layer: its pose is that
 * clip's sample, with no blend. */
static bool IsSingleClip(const AnimationGraphInstance* self, const AnimationGraphBinding* binding,
    const AnimationGraphBoundClip** outClip, double* outTime)
{
    if (AnimationGraph_GetLayerCount(self->Graph) != 1U)
    {
        return false;
    }
    const AnimationGraphLayer* Layer = GetLayer(self, 0U);
    const AnimationGraphLayerPlayback* Playback = &self->Layers[0];
    if ((Layer->_maskBoneCount > 0U) || !(Layer->_weight >= 1.0f) || (Playback->EntryCount != 1U)
        || !(Playback->Entries[0].Weight >= 1.0f))
    {
        return false;
    }
    const AnimationGraphState* State = AnimationGraph_GetLayerState(self->Graph, Layer, Playback->Entries[0].State);
    const AnimationGraphBoundClip* Clips = GetStateClips(binding, Layer, Playback->Entries[0].State);
    if ((State->_kind != AnimationGraphStateKind_Clip) || !Clips[0].IsFound)
    {
        return false;
    }
    *outClip = &Clips[0];
    *outTime = Playback->Entries[0].Time;
    return true;
}


// Public functions: the binding.
size_t AnimationGraphBinding_GetStorageSize(const AnimationGraph* graph, const GameSkeleton* skeleton)
{
    // The counts are bounded by the graph's limits and GAME_SKELETON_MAX_BONES: no overflow.
    size_t Clips = AlignUp((size_t)GetTotalClipCount(graph) * sizeof(AnimationGraphBoundClip));
    size_t StateClips = AlignUp((size_t)AnimationGraph_GetStateCount(graph) * sizeof(uint32_t));
    size_t Weights = AlignUp((size_t)AnimationGraph_GetLayerCount(graph) * (size_t)GameSkeleton_GetBoneCount(skeleton)
        * sizeof(float));
    return Clips + StateClips + Weights;
}

void AnimationGraphBinding_Build(const AnimationGraph* graph, const GameSkeleton* skeleton,
    const SkeletalBinding* libraries, uint32_t libraryCount, void* storage, AnimationGraphBinding* outBinding)
{
    Memory_Zero(outBinding, sizeof(*outBinding));
    outBinding->Graph = graph;
    outBinding->Skeleton = skeleton;
    outBinding->Libraries = libraries;
    outBinding->LibraryCount = (libraries != NULL) ? libraryCount : 0U;
    unsigned char* Cursor = storage;
    outBinding->Clips = (AnimationGraphBoundClip*)(void*)Cursor;
    Cursor += AlignUp((size_t)GetTotalClipCount(graph) * sizeof(AnimationGraphBoundClip));
    outBinding->StateClips = (uint32_t*)(void*)Cursor;
    Cursor += AlignUp((size_t)AnimationGraph_GetStateCount(graph) * sizeof(uint32_t));
    outBinding->BoneWeights = (float*)(void*)Cursor;

    uint32_t Next = 0U;
    for (uint32_t Index = 0U; Index < AnimationGraph_GetStateCount(graph); Index++)
    {
        const AnimationGraphState* State = AnimationGraph_GetState(graph, Index);
        outBinding->StateClips[Index] = Next;
        if (State->_kind == AnimationGraphStateKind_Clip)
        {
            BindClip(outBinding, AnimationGraph_GetString(graph, State->_clip), &outBinding->Clips[Next]);
        }
        for (uint32_t Point = 0U; (State->_kind == AnimationGraphStateKind_Blend) && (Point < State->_pointCount); Point++)
        {
            const AnimationGraphBlendPoint* Record = AnimationGraph_GetBlendPoint(graph, State->_firstPoint + Point);
            BindClip(outBinding, AnimationGraph_GetString(graph, Record->_clip), &outBinding->Clips[Next + Point]);
        }
        Next += GetStateClipCount(State);
    }
    uint32_t BoneCount = GameSkeleton_GetBoneCount(skeleton);
    for (uint32_t Layer = 0U; Layer < AnimationGraph_GetLayerCount(graph); Layer++)
    {
        BuildBoneWeights(outBinding, AnimationGraph_GetLayer(graph, Layer), outBinding->BoneWeights + (Layer * BoneCount));
    }
    BuildLookAt(outBinding);
}


// Public functions: the instance.
size_t AnimationGraphInstance_GetStorageSize(const AnimationGraph* graph)
{
    return AlignUp((size_t)AnimationGraph_GetParameterSlotCount(graph) * sizeof(float))
        + AlignUp((size_t)AnimationGraph_GetLayerCount(graph) * sizeof(AnimationGraphLayerPlayback));
}

void AnimationGraphInstance_Construct(AnimationGraphInstance* self, const AnimationGraph* graph, void* storage)
{
    Memory_Zero(self, sizeof(*self));
    self->Graph = graph;
    unsigned char* Cursor = storage;
    self->Parameters = (float*)(void*)Cursor;
    Cursor += AlignUp((size_t)AnimationGraph_GetParameterSlotCount(graph) * sizeof(float));
    self->Layers = (AnimationGraphLayerPlayback*)(void*)Cursor;
    for (uint32_t Index = 0U; Index < AnimationGraph_GetParameterSlotCount(graph); Index++)
    {
        self->Parameters[Index] = 0.0f;
    }
    for (uint32_t Index = 0U; Index < AnimationGraph_GetCustomParameterCount(graph); Index++)
    {
        const AnimationGraphParameter* Parameter = AnimationGraph_GetCustomParameter(graph, Index);
        if (Parameter->_type != AnimationGraphValueType_Trigger)
        {
            self->Parameters[Parameter->_index] = Parameter->_default;
        }
    }
    for (uint32_t Layer = 0U; Layer < AnimationGraph_GetLayerCount(graph); Layer++)
    {
        AnimationGraphLayerPlayback* Playback = &self->Layers[Layer];
        Memory_Zero(Playback, sizeof(*Playback));
        Playback->Current = AnimationGraph_GetLayer(graph, Layer)->_entryState;
        Playback->Entries[0] = (AnimationGraphEntry){ .State = Playback->Current, .Weight = 1.0f, .StartWeight = 1.0f,
            .TargetWeight = 1.0f };
        Playback->EntryCount = 1U;
    }
    UpdateLookAt(self, 0.0);
}

void AnimationGraphInstance_SetParameter(AnimationGraphInstance* self, uint32_t index, float value)
{
    if ((index >= AnimationGraph_GetParameterSlotCount(self->Graph)) || !isfinite(value))
    {
        return;
    }
    if (index < (uint32_t)AnimationGraphBuiltIn_Count)
    {
        self->Parameters[index] = value;
        return;
    }
    for (uint32_t Index = 0U; Index < AnimationGraph_GetCustomParameterCount(self->Graph); Index++)
    {
        const AnimationGraphParameter* Parameter = AnimationGraph_GetCustomParameter(self->Graph, Index);
        if ((Parameter->_type == AnimationGraphValueType_Trigger) || (Parameter->_index != index))
        {
            continue;
        }
        self->Parameters[index] = (Parameter->_type == AnimationGraphValueType_Bool) ? ((value > 0.5f) ? 1.0f : 0.0f)
            : Clamp(value, Parameter->_min, Parameter->_max);
        return;
    }
}

float AnimationGraphInstance_GetParameter(const AnimationGraphInstance* self, uint32_t index)
{
    return (index < AnimationGraph_GetParameterSlotCount(self->Graph)) ? self->Parameters[index] : 0.0f;
}

void AnimationGraphInstance_FireEvent(AnimationGraphInstance* self, uint32_t index)
{
    if (index < AnimationGraph_GetEventSlotCount(self->Graph))
    {
        self->PendingEvents |= (uint64_t)1U << index;
    }
}

void AnimationGraphInstance_EnterState(AnimationGraphInstance* self, uint32_t layer, uint32_t state, float fadeSeconds,
    bool isRestarted)
{
    if ((layer >= AnimationGraph_GetLayerCount(self->Graph)) || (state >= GetLayer(self, layer)->_stateCount))
    {
        return;
    }
    AnimationGraphLayerPlayback* Playback = &self->Layers[layer];
    float Fade = (isfinite(fadeSeconds) && (fadeSeconds > 0.0f)) ? fadeSeconds : 0.0f;
    AnimationGraphEntry* Entered = FindEntry(Playback, state);
    if (Entered == NULL)
    {
        if (Playback->EntryCount == ANIMATION_GRAPH_PLAYER_MAX_ENTRIES)
        {
            // Full: the lightest state goes (it was fading out).
            uint32_t Lightest = 0U;
            for (uint32_t Index = 1U; Index < Playback->EntryCount; Index++)
            {
                if (Playback->Entries[Index].Weight < Playback->Entries[Lightest].Weight)
                {
                    Lightest = Index;
                }
            }
            for (uint32_t Index = Lightest + 1U; Index < Playback->EntryCount; Index++)
            {
                Playback->Entries[Index - 1U] = Playback->Entries[Index];
            }
            Playback->EntryCount--;
        }
        Entered = &Playback->Entries[Playback->EntryCount];
        Playback->EntryCount++;
        *Entered = (AnimationGraphEntry){ .State = state, .Weight = 0.0f };
    }
    else if (isRestarted || Entered->HasEnded)
    {
        StartEntryOver(Entered);
    }
    else
    {
        // Back into a state still fading out: it keeps its place, and its exit times count from now.
        Entered->Cycles = 0.0;
    }
    Playback->Current = state;
    for (uint32_t Index = 0U; Index < Playback->EntryCount; Index++)
    {
        AnimationGraphEntry* Entry = &Playback->Entries[Index];
        StartFade(Entry, (Entry == Entered) ? 1.0f : 0.0f, Fade);
    }
    RemoveFadedOut(Playback);
}

void AnimationGraphInstance_Advance(AnimationGraphInstance* self, const AnimationGraphBinding* binding, double seconds,
    const AnimationGraphContext* context)
{
    self->Events = self->PendingEvents;
    self->PendingEvents = 0U;
    double Seconds = (isfinite(seconds) && (seconds > 0.0)) ? seconds : 0.0;
    for (uint32_t Layer = 0U; Layer < AnimationGraph_GetLayerCount(self->Graph); Layer++)
    {
        TakeTransitions(self, Layer, context);
        AnimationGraphLayerPlayback* Playback = &self->Layers[Layer];
        for (uint32_t Index = 0U; Index < Playback->EntryCount; Index++)
        {
            AdvanceEntry(self, binding, Layer, &Playback->Entries[Index], Seconds, context);
            AdvanceFade(&Playback->Entries[Index], (float)Seconds);
        }
        RemoveFadedOut(Playback);
    }
    UpdateLookAt(self, Seconds);
}

void AnimationGraphInstance_SamplePose(const AnimationGraphInstance* self, const AnimationGraphBinding* binding,
    Quaternion importRotation, Transform* scratch, SkeletalBlendBone* blend, Transform* outPose)
{
    const GameSkeleton* Skeleton = binding->Skeleton;
    uint32_t BoneCount = GameSkeleton_GetBoneCount(Skeleton);
    const AnimationGraphBoundClip* Single = NULL;
    double SingleTime = 0.0;
    if (IsSingleClip(self, binding, &Single, &SingleTime))
    {
        SkeletalBinding_SampleClip(&binding->Libraries[Single->Clip.Library], Single->Clip.Clip, (float)SingleTime,
            outPose);
    }
    else
    {
        SkeletalPose_SetRest(Skeleton, outPose);
        for (uint32_t Layer = 0U; Layer < AnimationGraph_GetLayerCount(self->Graph); Layer++)
        {
            SkeletalPose_ClearBlend(blend, BoneCount);
            float Total = AddLayerToBlend(self, binding, Layer, scratch, blend);
            SkeletalPose_BlendOver(blend, Total, binding->BoneWeights + (Layer * BoneCount), BoneCount, outPose);
        }
    }
    if ((binding->LookBoneCount == 0U) || !(self->LookWeight > 0.0f)
        || ((self->LookYaw == 0.0f) && (self->LookPitch == 0.0f)))
    {
        return;
    }
    // The turn in the object's space: the pitch about its right axis (up positive), then the yaw about its up axis
    // (toward +X positive, as a character's yaw); then into the skeleton's mesh space.
    Quaternion Yaw = QuaternionFromAxisAngle((Vector3){ 0.0f, 1.0f, 0.0f }, self->LookYaw);
    Quaternion Pitch = QuaternionFromAxisAngle((Vector3){ 1.0f, 0.0f, 0.0f }, -self->LookPitch);
    Quaternion ObjectTurn = QuaternionMultiply(Yaw, Pitch);
    Quaternion MeshTurn = QuaternionMultiply(QuaternionMultiply(QuaternionInvert(importRotation), ObjectTurn),
        importRotation);
    SkeletalPose_ApplyLookAt(Skeleton, outPose, binding->LookBones, binding->LookShares, binding->LookBoneCount,
        QuaternionNormalize(MeshTurn), self->LookWeight);
}

void AnimationGraphPlayer_GetBlendWeights(const AnimationGraph* graph, const AnimationGraphState* state, float value,
    float* outWeights)
{
    uint32_t Count = state->_pointCount;
    for (uint32_t Point = 0U; Point < Count; Point++)
    {
        outWeights[Point] = 0.0f;
    }
    if (Count == 0U)
    {
        return;
    }
    const AnimationGraphBlendPoint* First = AnimationGraph_GetBlendPoint(graph, state->_firstPoint);
    const AnimationGraphBlendPoint* Last = AnimationGraph_GetBlendPoint(graph, state->_firstPoint + Count - 1U);
    if (!(value > First->_at))
    {
        outWeights[0] = 1.0f;
        return;
    }
    if (value >= Last->_at)
    {
        outWeights[Count - 1U] = 1.0f;
        return;
    }
    for (uint32_t Point = 0U; (Point + 1U) < Count; Point++)
    {
        const AnimationGraphBlendPoint* Low = AnimationGraph_GetBlendPoint(graph, state->_firstPoint + Point);
        const AnimationGraphBlendPoint* High = AnimationGraph_GetBlendPoint(graph, state->_firstPoint + Point + 1U);
        if (value < High->_at)
        {
            float Share = (value - Low->_at) / (High->_at - Low->_at);
            outWeights[Point] = 1.0f - Share;
            outWeights[Point + 1U] = Share;
            return;
        }
    }
}
