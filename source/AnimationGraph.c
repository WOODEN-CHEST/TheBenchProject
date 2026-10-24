#include "AnimationGraph.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"

/*
 * Animation graphs (AnimationGraph.h): the built-in vocabulary, lookups by name, and copying and freeing the block. The
 * JSON reader is source/AnimationGraphReader.c, the asset type source/AnimationGraphDefinition.c.
 */


// Fields.
static const unsigned char* const BuiltInNames[AnimationGraphBuiltIn_Count] =
{
    [AnimationGraphBuiltIn_Speed] = u8"speed",
    [AnimationGraphBuiltIn_Speed3d] = u8"speed_3d",
    [AnimationGraphBuiltIn_ForwardSpeed] = u8"forward_speed",
    [AnimationGraphBuiltIn_SideSpeed] = u8"side_speed",
    [AnimationGraphBuiltIn_VerticalSpeed] = u8"vertical_speed",
    [AnimationGraphBuiltIn_IsGrounded] = u8"is_grounded",
    [AnimationGraphBuiltIn_IsTouchingGround] = u8"is_touching_ground",
    [AnimationGraphBuiltIn_IsFlying] = u8"is_flying",
    [AnimationGraphBuiltIn_IsCrouching] = u8"is_crouching",
    [AnimationGraphBuiltIn_CrouchAmount] = u8"crouch_amount",
    [AnimationGraphBuiltIn_TimeInAir] = u8"time_in_air",
    [AnimationGraphBuiltIn_TurnRate] = u8"turn_rate",
    [AnimationGraphBuiltIn_LookYaw] = u8"look_yaw",
    [AnimationGraphBuiltIn_LookPitch] = u8"look_pitch",
    [AnimationGraphBuiltIn_IsStandUpBlocked] = u8"is_stand_up_blocked",
    [AnimationGraphBuiltIn_LandingSpeed] = u8"landing_speed",
    [AnimationGraphBuiltIn_IsInAction] = u8"is_in_action",
    [AnimationGraphBuiltIn_MoveAmount] = u8"move_amount",
};

static const bool BuiltInBooleans[AnimationGraphBuiltIn_Count] =
{
    [AnimationGraphBuiltIn_IsGrounded] = true,
    [AnimationGraphBuiltIn_IsTouchingGround] = true,
    [AnimationGraphBuiltIn_IsFlying] = true,
    [AnimationGraphBuiltIn_IsCrouching] = true,
    [AnimationGraphBuiltIn_IsStandUpBlocked] = true,
    [AnimationGraphBuiltIn_IsInAction] = true,
};

static const unsigned char* const EventNames[AnimationGraphEvent_Count] =
{
    [AnimationGraphEvent_Jumped] = u8"jumped",
    [AnimationGraphEvent_Landed] = u8"landed",
    [AnimationGraphEvent_LeftGround] = u8"left_ground",
    [AnimationGraphEvent_Crouched] = u8"crouched",
    [AnimationGraphEvent_StoodUp] = u8"stood_up",
    [AnimationGraphEvent_TookOff] = u8"took_off",
    [AnimationGraphEvent_StoppedFlying] = u8"stopped_flying",
    [AnimationGraphEvent_Used] = u8"used",
    [AnimationGraphEvent_EndAction] = u8"end_action",
};


// Static functions.
static bool AreNamesEqual(const unsigned char* a, const unsigned char* b)
{
    size_t Length = StringUTF8_GetByteLength(a);
    return (Length == StringUTF8_GetByteLength(b)) && Memory_IsEqual(a, b, Length);
}


// Public functions: the vocabulary.
const unsigned char* AnimationGraph_GetBuiltInName(AnimationGraphBuiltIn parameter)
{
    return ((uint32_t)parameter < (uint32_t)AnimationGraphBuiltIn_Count) ? BuiltInNames[parameter] : NULL;
}

bool AnimationGraph_IsBuiltInBoolean(AnimationGraphBuiltIn parameter)
{
    return ((uint32_t)parameter < (uint32_t)AnimationGraphBuiltIn_Count) && BuiltInBooleans[parameter];
}

const unsigned char* AnimationGraph_GetBuiltInEventName(AnimationGraphEvent event)
{
    return ((uint32_t)event < (uint32_t)AnimationGraphEvent_Count) ? EventNames[event] : NULL;
}


// Public functions: copying and freeing.
AnimationGraph* AnimationGraph_Clone(const AnimationGraph* self)
{
    AnimationGraph* Copy = Memory_Allocate(self->_byteCount);
    Memory_Copy(self, Copy, self->_byteCount);
    return Copy;
}

void AnimationGraph_Destroy(AnimationGraph* self)
{
    Memory_Free(self);
}


// Public functions: lookups.
bool AnimationGraph_TryFindParameter(const AnimationGraph* self, const unsigned char* name, uint32_t* outIndex)
{
    if (name == NULL)
    {
        return false;
    }
    for (uint32_t Index = 0U; Index < (uint32_t)AnimationGraphBuiltIn_Count; Index++)
    {
        if (AreNamesEqual(BuiltInNames[Index], name))
        {
            if (outIndex != NULL)
            {
                *outIndex = Index;
            }
            return true;
        }
    }
    const AnimationGraphParameter* Parameter = AnimationGraph_FindCustomParameter(self, name);
    if ((Parameter == NULL) || (Parameter->_type == AnimationGraphValueType_Trigger))
    {
        return false;
    }
    if (outIndex != NULL)
    {
        *outIndex = Parameter->_index;
    }
    return true;
}

bool AnimationGraph_TryFindEvent(const AnimationGraph* self, const unsigned char* name, uint32_t* outIndex)
{
    if (name == NULL)
    {
        return false;
    }
    for (uint32_t Index = 0U; Index < (uint32_t)AnimationGraphEvent_Count; Index++)
    {
        if (AreNamesEqual(EventNames[Index], name))
        {
            if (outIndex != NULL)
            {
                *outIndex = Index;
            }
            return true;
        }
    }
    const AnimationGraphParameter* Parameter = AnimationGraph_FindCustomParameter(self, name);
    if ((Parameter == NULL) || (Parameter->_type != AnimationGraphValueType_Trigger))
    {
        return false;
    }
    if (outIndex != NULL)
    {
        *outIndex = Parameter->_index;
    }
    return true;
}

const AnimationGraphParameter* AnimationGraph_FindCustomParameter(const AnimationGraph* self, const unsigned char* name)
{
    if (name == NULL)
    {
        return NULL;
    }
    for (uint32_t Index = 0U; Index < self->_parameterCount; Index++)
    {
        const AnimationGraphParameter* Parameter = AnimationGraph_GetCustomParameter(self, Index);
        if (AreNamesEqual(AnimationGraph_GetString(self, Parameter->_name), name))
        {
            return Parameter;
        }
    }
    return NULL;
}

bool AnimationGraph_TryFindLayer(const AnimationGraph* self, const unsigned char* name, uint32_t* outIndex)
{
    for (uint32_t Index = 0U; (name != NULL) && (Index < self->_layerCount); Index++)
    {
        if (AreNamesEqual(AnimationGraph_GetString(self, AnimationGraph_GetLayer(self, Index)->_name), name))
        {
            if (outIndex != NULL)
            {
                *outIndex = Index;
            }
            return true;
        }
    }
    return false;
}

bool AnimationGraph_TryFindState(const AnimationGraph* self, uint32_t layer, const unsigned char* name,
    uint32_t* outIndex)
{
    if ((name == NULL) || (layer >= self->_layerCount))
    {
        return false;
    }
    const AnimationGraphLayer* Layer = AnimationGraph_GetLayer(self, layer);
    for (uint32_t Index = 0U; Index < Layer->_stateCount; Index++)
    {
        if (AreNamesEqual(AnimationGraph_GetString(self, AnimationGraph_GetLayerState(self, Layer, Index)->_name), name))
        {
            if (outIndex != NULL)
            {
                *outIndex = Index;
            }
            return true;
        }
    }
    return false;
}

bool AnimationGraph_TryFindAction(const AnimationGraph* self, const unsigned char* name, uint32_t* outIndex)
{
    for (uint32_t Index = 0U; (name != NULL) && (Index < self->_actionCount); Index++)
    {
        if (AreNamesEqual(AnimationGraph_GetString(self, AnimationGraph_GetAction(self, Index)->_name), name))
        {
            if (outIndex != NULL)
            {
                *outIndex = Index;
            }
            return true;
        }
    }
    return false;
}
