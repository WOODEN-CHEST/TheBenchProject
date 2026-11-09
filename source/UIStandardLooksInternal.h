#pragma once
#include "UIBoxStyle.h"
#include "UIKit.h"
#include "UIPlacement.h"


/*
 * Private to the kit's standard looks: UIStandardLooks.c (the base elements, the tooltip, the backdrop) and UIStandardValueLooks.c (the
 * value elements: the property box, the labelled element, the form, the compound slider, the row of fields).
 */


/* How thick a box's border is on the screen, in its own UI pixels: a flat box's outline, a nine-slice's margins at their size; none else
 * (no box, no texture). What a text box's bars and a property box's fill stand inside. */
UIInsets UIStandardLooks_GetBorderInsets(const UIBoxStyle* style);

/* Registers the standard look of each value element's kind (UIStandardValueLooks.c); UIStandardLooks_Register calls it. */
Error UIStandardValueLooks_Register(UIKit* kit);
