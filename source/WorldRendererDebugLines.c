// A list of debug lines handed to the renderer (WorldRenderer_SetDebugLines), drawn in the scene pass in render space. Reads
// only.

#include "WorldRendererInternal.h"
#include "GameMath.h"
#include "raylib/rlgl.h"


// Macros.
/** How opaque the part of a line is that lies behind something. */
#define HIDDEN_ALPHA 70
/** The value a line's brightest channel reaches after the exposure, low on the tone curve where colours stay themselves. */
#define TONE_SHARE 0.5f


// Static functions.
static void DrawLines(const WorldDebugLine* lines, size_t count, bool isHiddenPass, float brightness, Vector3Double origin)
{
    for (size_t Index = 0U; Index < count; Index++)
    {
        const WorldDebugLine* Line = &lines[Index];
        Color Shown = Line->LineColor;
        Shown.r = (unsigned char)((float)Shown.r * brightness);
        Shown.g = (unsigned char)((float)Shown.g * brightness);
        Shown.b = (unsigned char)((float)Shown.b * brightness);
        if (isHiddenPass)
        {
            Shown.a = (unsigned char)(((int)Shown.a * HIDDEN_ALPHA) / 255);
        }
        DrawLine3D(GameMath_ToVector3Relative(Line->From, origin), GameMath_ToVector3Relative(Line->To, origin), Shown);
    }
}


// Internal functions (declared in WorldRendererInternal.h).
void WorldRendererDebugLines_Draw(WorldRenderer* self, const WorldRenderFrame* frame)
{
    if ((self->_debugLines == NULL) || (self->_debugLineCount == 0U))
    {
        return;
    }
    // The picture goes through the eye's exposure and the tone curve, which would wash a colour out at any bright value:
    // against them, a line shows about the colour it asks for, whatever the eye is adapted to.
    float Brightness = (frame->LastExposure > TONE_SHARE) ? (TONE_SHARE / frame->LastExposure) : 1.0f;
    // Twice, as the colliders' wireframes: faint without the depth test (what lies behind something, or on a surface whose
    // depth it would fight), then solid with it. rlgl batches its lines, so each depth state is drawn out before it changes.
    rlDrawRenderBatchActive();
    rlDisableDepthTest();
    DrawLines(self->_debugLines, self->_debugLineCount, true, Brightness, frame->Origin);
    rlDrawRenderBatchActive();
    rlEnableDepthTest();
    DrawLines(self->_debugLines, self->_debugLineCount, false, Brightness, frame->Origin);
}
