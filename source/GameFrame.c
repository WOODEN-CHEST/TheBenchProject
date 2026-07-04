#include "GameFrame.h"


// Public functions.
Error GameFrame_Construct(GameFrame* self, const GameFrameVTable* vtable, const unsigned char* debugName)
{
    if ((self == NULL) || (vtable == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "GameFrame_Construct: self and vtable must not be NULL.");
    }

    self->VTable = vtable;
    self->IsUpdated = true;
    self->IsRendered = true;
    self->CompositeColor = RenderColor_White();
    self->DebugName = debugName;

    Error LoadEventResult = WREvent_Construct1(&self->OnLoad);
    if (LoadEventResult.Code != ErrorCode_Success)
    {
        return LoadEventResult;
    }

    Error UnloadEventResult = WREvent_Construct1(&self->OnUnload);
    if (UnloadEventResult.Code != ErrorCode_Success)
    {
        WREvent_Deconstruct(&self->OnLoad);
        return UnloadEventResult;
    }

    return Error_CreateSuccess();
}

void GameFrame_Deconstruct(GameFrame* self)
{
    if (self == NULL)
    {
        return;
    }

    WREvent_Deconstruct(&self->OnLoad);
    WREvent_Deconstruct(&self->OnUnload);
}
