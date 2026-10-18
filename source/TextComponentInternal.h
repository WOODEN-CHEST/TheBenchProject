#pragma once
#include <stddef.h>
#include <stdint.h>
#include "TextComponent.h"
#include "wr/WRMemory.h"
#include "wr/WRError.h"


/*
 * Private glue between TextComponent.c and TextComponentFactory.c; not part of the public API.
 *
 * The factory borrows a pool slot and initializes it with one of the *_Initialize functions below. Components call
 * back into their factory for the buffers that hold their strings and child lists, and for fresh revision numbers.
 */


// Factory services (TextComponentFactory.c).
/* Borrows an empty buffer with the given element size from the factory's buffer pool. */
Error TextComponentFactory_BorrowBuffer(TextComponentFactory* self, size_t elementSize, GenericBuffer** outBuffer);

/* Returns a buffer obtained from TextComponentFactory_BorrowBuffer; a failure is released (the pool keeps the rest). */
void TextComponentFactory_ReturnBuffer(TextComponentFactory* self, GenericBuffer* buffer);

/* Returns a new factory-wide unique revision number (never 0). */
uint64_t TextComponentFactory_NextRevision(TextComponentFactory* self);

/* Returns a component and its whole subtree to the factory. The caller has already unlinked @p root from any parent
 * (or is tearing down the parent too). */
Error TextComponentFactory_ReturnSubtree(TextComponentFactory* self, TextComponent* root);


// Holding (TextComponent.c). TextDocument marks the root it owns, so the tree API refuses to give it away.
void TextComponent_SetIsHeld(TextComponent* self, bool isHeld);

// Component initialization (TextComponent.c). Each takes a zero-filled slot and gives it the type's defaults.
void EmptyComponent_Initialize(EmptyComponent* self, TextComponentFactory* factory);
void StringComponent_Initialize(StringComponent* self, TextComponentFactory* factory);
void SpriteComponent_Initialize(SpriteComponent* self, TextComponentFactory* factory);
/* Leaves the key unset: the factory sets it right after (TranslateComponent_SetKey) or returns the component. */
void TranslateComponent_Initialize(TranslateComponent* self, TextComponentFactory* factory);
