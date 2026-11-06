#include "UIScalePolicy.h"
#include "Config.h"
#include "wr/WRMath.h"


// Static functions.
/* Whether a length or a scale is a finite number above 0 (the > also refuses a NaN). */
static bool IsUsable(float value)
{
    return (value > 0.0f) && !Math_IsInfinityFloat(value);
}

/* The monitor's scale: its height against the reference, in whole steps, never below 1: a small monitor gets the same
 * pixels as 1080p rather than a blurred UI. */
static float GetMonitorScale(float monitorHeight)
{
    float Steps = Math_FloorFloat(((monitorHeight / UI_SCALE_POLICY_REFERENCE_HEIGHT) * UI_SCALE_POLICY_MONITOR_STEPS) + 0.5f);
    return Math_MaxFloat(1.0f, Steps / UI_SCALE_POLICY_MONITOR_STEPS);
}


// Public functions.
float UIScalePolicy_GetScale(const UIScalePolicy* policy, float playerScale, float windowHeight, float monitorHeight)
{
    float Player = IsUsable(playerScale) ? playerScale : 1.0f;
    float Scale = Player;
    if (policy != NULL)
    {
        switch (policy->Kind)
        {
            case UIScalePolicyKind_Fixed:
                Scale = IsUsable(policy->FixedScale) ? policy->FixedScale : Player;
                break;
            case UIScalePolicyKind_ConstantPixels:
                Scale = IsUsable(monitorHeight) ? (GetMonitorScale(monitorHeight) * Player) : Player;
                break;
            case UIScalePolicyKind_FollowHeight:
                Scale = IsUsable(windowHeight) ? ((windowHeight / UI_SCALE_POLICY_REFERENCE_HEIGHT) * Player) : Player;
                break;
        }
    }
    return Math_ClampFloat(Scale, CONFIG_MIN_UI_SCALE, UI_SCALE_POLICY_MAX_SCALE);
}
