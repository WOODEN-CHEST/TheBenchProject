#include <stddef.h>
#include "Services.h"
#include "wr/WRCompile.h"
#include "wr/WRMemory.h"


// Public functions.
void Services_Construct(Services* self)
{
    if (self == NULL)
    {
        return;
    }
    Memory_Zero(self, sizeof(*self));
}

void Services_Deconstruct(Services* self)
{
    // Services owns nothing; teardown of the referenced services is the composition root's responsibility.
    UNUSED(self);
}
