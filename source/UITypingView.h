#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "ProgramTime.h"
#include "TextDocument.h"
#include "TextLayout.h"
#include "UIElement.h"
#include "UITextBox.h"


/*
 * Private to the kit's standard looks (UIStandardLooks.c, UIStandardValueLooks.c): what a look draws a text box's typing with, the text
 * box's own and a property box's (which types in a text box of its own). It owns a document built from the box's content (again when the
 * content's revision, the mask or the plain text's colour moves), a layout of it with carets, and the caret's blink; it draws the
 * selection behind the text, only the view's part of the text, and the caret. A look gives the theme's parts for each.
 */


// Types.
/* A look's typing view. Construct with UITypingView_Construct; the layout points into the document. */
typedef struct UITypingViewStruct
{
    TextDocument Document;
    TextLayout Layout;
    bool HasBuilt;
    uint64_t BuiltContentRevision;
    uint32_t BuiltMask;
    Color BuiltColor;
    bool HasAppliedTheme;
    uint64_t AppliedThemeRevision;
    double BlinkSeconds;
    uint64_t SeenCaretRevision;
} UITypingView;

/* The theme's parts a look draws the typing with: its kind's selection (and the one shown while the box is not entered), caret and
 * overwrite caret boxes, and the caret's width. A part the kind does not have is -1 (drawn with the one that stands in for it). */
typedef struct UITypingPartsStruct
{
    UIElementKind Kind;
    int32_t Selection;
    int32_t InactiveSelection;
    int32_t Caret;
    int32_t OverwriteCaret;
    int32_t CaretWidth;
} UITypingParts;


// Functions.
/* Builds the view's document and layout from the kit's factories. Nothing is left on failure. */
Error UITypingView_Construct(UITypingView* self, UIKit* kit);

/* Releases them (the layout first: it points into the document). */
Error UITypingView_Deconstruct(UITypingView* self);

/* Builds the document again when the box's content, the theme's font, the mask or the plain text's colour changed. */
Error UITypingView_RefreshDocument(UITypingView* self, const Widget* widget, UITextBoxCapability* textBox, uint32_t mask, Color textColor);

/* Lays the text out (the theme's text size times the widget's effective scale), with carets, wrapped at @p wrapWidth UI pixels when
 * @p isWrapped: what a look's UITextBoxGeometry.LayOut answers, after refreshing the document. */
Error UITypingView_LayOut(UITypingView* self, const Widget* widget, bool isWrapped, float wrapWidth, const TextLayout** outLayout,
    float* outUIPixelsPerUnit);

/* The view's part of a tick: the caret shows at once after an edit or a move, then blinks; the document's sprites advance. */
Error UITypingView_Update(UITypingView* self, const UITextBoxCapability* textBox, ProgramTime time);

/* The view's box on the surface, in whole pixels, for a widget drawn at @p pixels: the box's view (UITextBoxCapability_GetViewBox) at the
 * widget's scale. */
Rectangle UITypingView_GetViewPixels(const Widget* widget, const UITextBoxCapability* textBox, Rectangle pixels);

/* Draws the selection behind the text, the text (only the view's part of it) or the placeholder, and the caret, clipped to @p view. */
Error UITypingView_Render(const UITypingView* self, const UIElementLook* look, const Widget* widget, const UITextBoxCapability* textBox,
    UIRenderContext* context, const UIThemeStyle* style, const UITypingParts* parts, Rectangle view, const UIElementText* placeholder,
    Color placeholderColor);
