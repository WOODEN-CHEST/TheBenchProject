# UI elements: the kit, themes and looks

The elements calling code builds a UI from: panels, labels, buttons, toggles, sliders, stacks, scroll areas and scroll bars,
dropdowns, menus, menu bars, status bars, text boxes, property boxes, the compounds final screens use (labelled rows, forms, compound
sliders, rows of fields), what an editor's windows are built from (lists, trees, modal dialogs, colour pickers, tool bars, drag and drop) and
the windows themselves with their types, sized in UI pixels and drawn through a theme. They stand on the framework in [UIFramework.md](UIFramework.md) (screens, widgets, input routing,
notifications).

| Module | Files | What it is |
|---|---|---|
| UIPlacement | `include/UIPlacement.h`, `source/UIPlacement.c` | Pure: placements (anchors and UI pixel offsets), insets, a stack's hints, turning a placement into a widget's normalized box. |
| UIBoxStyle | `include/UIBoxStyle.h`, `source/UIBoxStyle.c` | How a box is drawn: nothing, flat or nine-slice, a colour per state. The nine quads and resolving a state are pure. |
| UITheme | `include/UITheme.h`, `source/UITheme.c`, `source/UIThemeDefinition.c`, private `source/UIThemeInternal.h` | The `ui_theme` asset type: the look of every element kind as data. |
| UIElement | `include/UIElement.h`, `source/UIElement.c` | What every element kind shares: its options, its events and cues, plain text content, icons, the press behaviour, and the helpers looks draw with. |
| UIKit | `include/UIKit.h`, `source/UIKit.c`, private `source/UIKitInternal.h` | The one way to make elements, over one widget factory: capabilities, the looks registered by kind and name, the widget types it composes. |
| UIStandardLooks | `include/UIStandardLooks.h`, `source/UIStandardLooks.c`, `source/UIStandardValueLooks.c`, `source/UIStandardEditorLooks.c`, private `source/UIStandardLooksInternal.h` and `source/UITypingView.{h,c}` | The kit's standard look (`tbp:test_box`) for every kind but the label's; the widget that shows hover text; a modal popup's backdrop; the widget that shows a drag. `UITypingView` is what the text box's and the property box's looks draw typing with. |
| StackWidget | `include/StackWidget.h`, `source/StackWidget.c` | Children in a row or a column. Its arithmetic (`UIStackLayout`) is shared with the panel. |
| UIPanel | `include/UIPanel.h`, `source/UIPanel.c` | A background and padding for what it holds. |
| UILabel | `include/UILabel.h`, `source/UILabel.c`, with `LabelWidget` | `LabelWidget` as an element of the kit. |
| UIButton, UIToggle, UISlider | `include/UI<Kind>.h`, `source/UI<Kind>.c` | The element kinds with input. |
| UIValue | `include/UIValue.h`, `source/UIValue.c`, `source/UIValueText.c` | The value piece: a number with its limits, soft range, step or value list, conversion and default, its pure arithmetic, its text that reads back, its clipboard and context menu, and the change helpers its elements post with. |
| UIPropertyBox | `include/UIPropertyBox.h`, `source/UIPropertyBox.c` | A value in a box, typed in or scrubbed sideways: Blender's field; a string too. |
| UILabelled | `include/UILabelled.h`, `source/UILabelled.c` | A label and any element (the labelled element), and a column whose labelled rows line up (the form). |
| UICompoundSlider | `include/UICompoundSlider.h`, `source/UICompoundSlider.c` | A slider, a property box and a name sharing one value. |
| UIVectorRow | `include/UIVectorRow.h`, `source/UIVectorRow.c` | Two to four property boxes for a vector's components, each event saying its axis. |
| UIScrollable | `include/UIScrollable.h`, `source/UIScrollable.c` | The scrollable piece: content and view sizes, the clamped offset, a mode per axis; its arithmetic pure. |
| UIScrollbar, UIScrollArea | `include/UI<Kind>.h`, `source/UI<Kind>.c` | A bar bound to another element's scrollable piece; content larger than its box, with two bars. |
| UIChoice | `include/UIChoice.h`, `source/UIChoice.c` | The choice piece: options as texts or from providers, the selection, mixed. |
| UIRowRecycler | `include/UIRowRecycler.h`, `source/UIRowRecycler.c` | Widgets for the rows in view only, reused as the view scrolls. |
| UIDropdown | `include/UIDropdown.h`, `source/UIDropdown.c` | One choice of several: a head and a list in a popup. |
| UIMenu | `include/UIMenu.h`, `source/UIMenu.c`, private `source/UIMenuInternal.h` | Menus as data, each open level a popup list of items. |
| UIMenuBar, UIStatusBar | `include/UI<Kind>.h`, `source/UI<Kind>.c` | A strip of entries opening menus; a strip of sections and a line of text. |
| UITextEdit | `include/UITextEdit.h`, `source/UITextEdit.c` | The text piece, pure: a UTF-8 text with its limits, a caret and a selection, words, edits with the incoming text's rules, and a grouped undo with a budget. |
| UITextBox | `include/UITextBox.h`, `source/UITextBox.c`, `source/UITextBoxInput.c`, private `source/UITextBoxInternal.h` | Text to type in or to read and select, on one line or several: the keys, the mouse, the clipboard, undo, scrolling, a placeholder, masked and read-only boxes, style ranges. |
| UIList | `include/UIList.h`, `source/UIList.c`, private `source/UIListRow.{h,c}` (the rows a list and a tree recycle) | Rows of the caller's data, only those in view made: selection by data index, toggle cells, keys once entered, a drag source and a drop target. |
| UITree | `include/UITree.h`, `source/UITree.c` | Nodes of the caller's, walked into nesting rows: expanding by the caller's state, selection by node, the list's keys and rows. |
| UIDialog | `include/UIDialog.h`, `source/UIDialog.c` | A modal question: a title, a message, content, buttons with an answer each; Enter and Escape in one press from an entered field. |
| UIColorPicker | `include/UIColorPicker.h`, `source/UIColorPicker.c` | A swatch and the popup that picks a `Color` or a `RenderColor`: a square, strips, channel fields and hex; its HSV conversions pure. |
| UIToolbar | `include/UIToolbar.h`, `source/UIToolbar.c` | A strip of the kit's buttons from a list: buttons, toggles, choices of a group and separators, across or upright. |
| UIWindow | `include/UIWindow.h`, `source/UIWindow.c` | A window: a frame that moves, resizes, minimizes, maximizes and closes, kept reachable, with content calling code fills; its pure arithmetic. |
| UIWindowTypes | `include/UIWindowTypes.h`, `source/UIWindowTypes.c` | The windows a UI opens by key: a registry of types in the root, opening one (a single one brought back), where the last one closed, a Window menu's items. |
| UIGalleryFrame | `include/UIGalleryFrame.h`, `source/UIGalleryFrame.c`, private `source/UIGalleryWindows.{h,c}` (the windows page) | Every element in every state, on seven pages: `--ui-gallery`. |

## Using it

```c
UIWidgetFactory Factory;                 // one kit per factory
TextComponentFactory Text;
UIKit Kit;
UIScreen Screen;
UIWidgetFactory_Construct(&Factory);
TextComponentFactory_Construct(&Text);
UIKit_Construct(&Kit, &Factory, &Text, Services->Assets, Services->Logger);
UIScreen_Construct(&Screen, &Factory, Services->Logger);
UIScreen_SetTheme(&Screen, Services->Assets, u8"tbp:test_flat");
UIWindowHost Host;  // a UI that fills the window: each tick UIWindowHost_Update sets its viewport and scale and updates it
UIWindowHost_Construct(&Host, UIScalePolicy_ConstantPixels(), Services->Config->UIScale, Aspect, AreaPosition);

UIStackOptions Page = UIStackOptions_Create(UIStackDirection_Column);
Page.Layout.Padding = UIInsets_All(12.0f);
Page.Layout.Spacing = 8.0f;
Page.Element.HasPlacement = true;
Page.Element.Placement = UIPlacement_Stretch(UIInsets_All(0.0f));
uint64_t PageId;
UIKit_CreateStack(&Kit, &Screen, WIDGET_ID_INVALID, &Page, &PageId);

UIButtonOptions Save = UIButtonOptions_Create(u8"Save");
uint64_t SaveId;
UIKit_CreateButton(&Kit, &Screen, PageId, &Save, &SaveId);
UIScreen_Subscribe(&Screen, SaveId, UI_EVENT_KIND_MASK(UIEventKind_Widget), OnSave, &Data, &Subscription);

UIButton_SetText(&Screen, SaveId, u8"Saved");   // by id, always: a stale id is refused, never a crash
```

Teardown: the screen (it destroys the elements), the kit, then the factories. The kit refuses to go while widgets of its looks live
(`ErrorCode_InvalidOperation`, the kit left as it was): they find their hooks through it. Every creation is whole or nothing: on a failure
no widget stays behind and the id is `WIDGET_ID_INVALID`. Widgets added during a tick are placed from the next tick on, like
every widget attached during a tick.

## An element: behaviour and a look

An element is two things kept apart.

- Its **behaviour** is a capability struct holding logical state and its contract (`UIButtonCapability`: its text and icon as plain
  content, its enabled flag, its press), with helpers that interpret input and post events (`UIButtonCapability_OnMouseInput`). Calling code
  talks only to it, by widget id (`UIButton_SetText(screen, id, text)`), never to a widget type or a look. No capability holds a look, a colour
  or laid-out text: a text is a `UITextContent` (an owned copy of the UTF-8 and a revision), an icon a `UIElementIcon`.
- Its **look** is a widget type that embeds the capability and draws: theme lookups, state fades (`UIElementLook`), laid-out text
  (`UIElementText`), icons (`UIElementIconView`) and animation state live there. A look picks those up through the helpers and is free to ignore
  them: a 3D sign standing for a button draws nothing in 2D at all.

A variant costs, from cheapest:
1. **Theme data:** colours (from the palette), sizes, nine-slice art. Almost everything.
2. **A named variant in the theme** (`UIElementOptions.Variant`, `UIElement_SetVariant`).
3. **A look of its own** (below): Render and Measure, maybe Update.

**Looks.** A look is a struct that starts with its kind's look base (`UIPanelLookBase`, `UIButtonLookBase`, `UIToggleLookBase`,
`UISliderLookBase`, `UIScrollbarLookBase`, `UIScrollAreaLookBase`, `UIDropdownLookBase`, `UIMenuBarLookBase`, `UIStatusBarLookBase`, `UITextBoxLookBase`,
`UIPropertyBoxLookBase`, `UILabelledLookBase`, `UIFormLookBase`, `UICompoundSliderLookBase`, `UIVectorRowLookBase`, `UIListLookBase`, `UITreeLookBase`,
`UIDialogLookBase`, `UIColorPickerLookBase`, `UIToolbarLookBase`, `UIWindowLookBase`: the `Widget`, then the capability) and a `UILookVTable`:

| Slot | | What it does |
|---|---|---|
| `Render` | required | draws, reading state (`UIButtonCapability_GetState` gives the state to show) |
| `Measure` | required | the preferred size in its own UI pixels |
| `Update` | optional | its part of a tick, after the behaviour's: fades, laid-out text, animation |
| `Construct`, `Deconstruct` | optional | its own state, after the capability is built and before it is released, with the kind's arguments (`UIButtonArgs`: the kit and the options) |
| `Layout` | optional | for a kind that holds children (the panel): lays them out inside its frame, in place of the kind's arrangement |
| `HitTest` | optional | answers whether a point holds it, in place of its box (a round button, a world object) |
| `GetThemeLook` | optional | its `UIElementLook`: a look that reads the theme declares the look capability through it, so variants work on it |

`UIKit_RegisterLook(kit, kind, name, sizeof(MyLook), &MyLookVTable, &typeId)` composes the widget type: the kind answers the mouse, the keys, the
wheel, a lost press, entering, leaving and hover (and posts the cues), and the look draws. `tests/UIElementTests.c` has a look with Render and
Measure alone (`OwnButtonLook`), the gallery another (the diamond). Looks are registered by kind and a name, a namespaced key (`tbp:test_box`); the
kit registers its standard look for every kind under `UI_KIT_STANDARD_LOOK` (`UIStandardLooks`: theme-driven boxes, flat or nine-slice, the example to
follow). An element is made with the look its options name (`UIElementOptions.Look`), else the one the screen's theme names for its kind (`"look"` in
the kind's block), else the standard one; a name no look answers gets the standard look and is logged once (per name). **Looks are fixed at
creation:** swapping the theme changes the values a look reads, never an element's widget type. The looks of the label, the tooltip, a menu
(its list and items), a dropdown's open list and rows, a menu bar's entries, the backdrop, a list's and a tree's rows, a colour picker's popup (its square,
strips and fields' box) and what shows a drag are not replaceable yet (`// TODO:` in `UIKit.c`'s kinds): `UIKit_RegisterLook` refuses the label, the tooltip,
the menu, the backdrop and the drag kinds, and the other parts are the kit's own widget types.

A slider look also tells the behaviour how long its thumb is (`UISliderCapability_SetThumbLength`, in its own UI pixels, from its Update): a press within
the thumb holds it where it was grabbed, and the thumb's centre travels the track less its length. A look that never says has a thumb of no length: the
pointer is the value. It draws the thumb at `UISliderCapability_GetShare` (through the value's conversion) along `UISliderCapability_GetOrientation`: from
the left, or from the bottom when upright, where the standard look swaps the theme's `thumb_size`, `track_thickness` and `min_size` across and along.

A property box look gives its text box the same geometry (its look base holds a text box's capability right after the widget) and draws, while
`UIPropertyBoxCapability_IsTyping`, that box's text, selection and caret (the standard looks share `UITypingView` for both); otherwise its shown text
(`UIPropertyBoxCapability_GetShownText`), the unit (`_GetUnit`, when `_ShowsUnit`) and the fill bar (`_ShowsFill`, `_GetFillShare`). A compound's look tells its
behaviour the theme's metrics from its Update (`UILabelledCapability_SetSpacing`, `UIFormCapability_SetMetrics`, `UICompoundSliderCapability_SetMetrics`,
`UIVectorRowCapability_SetMetrics`) and measures through the kind (`UILabelled_Measure`, `UIForm_Measure`, `UICompoundSlider_Measure`,
`UIVectorRow_Measure`); the kind lays its parts out. The list, the tree and the dialog are told their metrics the same way (`UIListCapability_SetMetrics`,
`UITreeCapability_SetMetrics`, `UIDialogCapability_SetMetrics`, `UIWindowCapability_SetMetrics`) and measure through the kind; a tool bar look lays its buttons out and measures them through
`UIToolbar_Arrange` and `UIToolbar_Measure` with the theme's metrics, and draws its separators where its entries say (`UIToolbarCapability_GetEntryAt`).

A text box look tells the behaviour where its text is (`UITextBoxCapability_SetGeometry`, in its Construct): its text's insets, a layout of the text with
carets at a wrap width (`TextLayoutOptions.TracksOffsets`; the look owns the document and the layout, so the capability holds no laid-out text), a line's
height and the border its bars stand inside. The behaviour asks it to place the caret under the pointer, to move a line up or down, to keep the caret in view
and to decide its bars. It builds its document from the box's content (`UITextBoxCapability_BuildDocument`, again when the content's revision, the mask or
the text colour moves: the colour is baked into the components, so style ranges keep theirs) and draws only the view's part of the text
(`TextLayoutDrawArguments.CullBox`): a long text costs little more than what shows.

**Finding the kit.** `UIKit_FromScreen` goes through the screen's factory, which is how the by-id functions find their capability
ids. A second kit over a factory is refused.

### Adding an element kind
1. A value in `UIElementKind`, a part enum and a row in `Kinds` (`source/UITheme.c`): the theme's reader and its checks come from
   that table, up to `UI_THEME_MAX_PARTS` values per kind.
2. If calling code talks to it, a value in `UIElementCapability`, and a capability struct holding behaviour alone (content as `UITextContent` and
   `UIElementIcon`, never a look) with its helpers and its by-id functions, in a module of its own; a look base struct (`Widget`, then the capability).
   A capability may embed pieces calling code also reaches by id (the dropdown's holds the choice piece; the scroll area's the scrollable piece): the kind
   declares each, up to `UI_KIND_MAX_CAPABILITIES`.
3. Its `UIKindBehaviour` (`source/UIKitInternal.h`: the capabilities and their resolvers, the flags its widgets start with, its hooks for construction, the
   behaviour's update, input, entering, leaving, hover and a descendant's focus) and its case in `GetBehaviour` (`source/UIKit.c`), and its kit creation
   function, which resolves the look (`ResolveLookType`). A kind made of parts the kit adds (the scroll area's bars, the status bar's sections) gets them
   from that function, once the element is attached, each of the look the theme names for its own kind.
4. Its standard look in `UIStandardLooks.c`, registered in `UIStandardLooks_Register`.
5. Its section in `references/asset_structure.md`, "UI themes", and the gallery.

## The elements

**Stack** (`StackWidget.h`). Children in a row or a column, with padding and spacing. Along the main axis each child is `Fixed`,
`Content` or `Fill` with a weight, across it `Start`, `Center`, `End` or `Stretch`, or a cross size of its own: a hint carried by the
child (`UIElementOptions.Hint`, `StackWidget_SetChildHint`). A child that is not rendered takes no room. Asked for its preferred size a stack
asks each child about the width it will get (what fills, about its share), so what wraps answers for that width and a row is as tall as what it holds. What overflows is laid
out as it is. A stack owns its children's boxes (it writes their placements each tick) and takes no input: the pointer goes through
it, its children still being hit.

**Panel** (`UIPanel.h`). A background, and its children stacked as a stack's are. Padding and spacing come from the theme unless its
options give them. It takes input and is not focusable, so a click on it does not reach what lies behind.

**Label** (`UILabel.h`, `LabelWidget.h`). Takes its font, its text size and its colours from the theme, or its text size from its
options (`UILabel_SetTextSize`). It takes no input. With `IsWrapped` it answers a width it is asked about from a second layout, while the text it draws stays
wrapped to its box; its preferred width rounds up to a whole surface pixel, and it wraps with half a pixel to spare, so a box made for its text keeps the text
on the lines it was measured with. The rest of `LabelWidget.h` stays there for rich text.

**Button** (`UIButton.h`). Activates by its click mode (below). Enter (the `ui_accept` action's keys) or Space while focused activates at the
press in every mode: handling the press owns the key, so its repeats do not activate again, and the space that key types goes nowhere. An optional toggled look (`selected`).
An optional icon (`UIButtonOptions.Icon`, `UIButton_SetIcon`): a placeholder letter or a sprite animation by name; the standard look draws it before the text,
`icon_spacing` (a custom value of the button's theme block, 4 by default) apart.
With `TakesWheel` (`UIButton_SetTakesWheel`) it posts `Scrolled` and stops the wheel; without it the wheel goes on up the tree. **Disabled is the capability's state,
not the input flag:** an input-disabled widget is transparent to the pointer, and a click on a disabled button must not reach what
lies behind it, so a disabled button takes the click and reports nothing, nor the wheel if it takes that. It is not focusable while disabled.

**Toggle** (`UIToggle.h`). On, off or mixed; an activation from mixed turns it on. The same click modes and activation buttons, Enter or Space. Disabled, it is as a disabled button.

**The value piece** (`UIValue.h`). A number, as a double, that a slider, a property box and a compound slider all hold and answer through the same functions
by id (`UIValue_Set`, `_Get`, `_SetRange`, `_SetSoftRange`, `_SetStep`, `_SetIsInteger`, `_SetValueList`, `_SetConversion`, `_SetCustomConversion`,
`_SetDefault`, `_ResetToDefault`, `_SetFineFactor`, `_SetMixed`, `_IsMixed`, `_Step`, `_SetIsEnabled`, `_GetSettings`), so calling code does not care which
it has. Its settings (`UIValueSettings`, pure) are hard limits each with a flag of its own (`HasMinimum`, `HasMaximum`: a value may have none, one or both),
a soft range inside them (its ends on the steps, or whole numbers, or on a hard limit, so no value lands between two steps), whole numbers, a step (a normal
double, at least 2^-50 of the largest limit's size, so steps can be told apart there) *or* a list of allowed values (rising strictly, at most
`UI_VALUE_MAX_LIST_COUNT`, inside the limits), a conversion (linear; log, its hard minimum above 0; power with an exponent; even, each value of a list the
same length of track; a custom pair of functions whose answers are checked at every call: a NaN or a value out of range is clamped, never stored), a
default, `FineFactor` (how much Shift slows a drag, 0.1) and `IsSinglePrecision` (the value stands for a float: its text is a float's shortest, typed text
reads as a float). Every setter checks the whole result (`UIValueSettings_IsValid` names what is wrong) and refuses one that breaks a rule, changing
nothing. **The order of operations, always:** the pointer, its share of the track (`UIValue_GetPointerFraction`), the conversion, the step or the list, the
limits (`UIValueSettings_FromShare`). The drag range a track or a scrub spans is the soft range, else a list's ends, else both hard limits
(`UIValueSettings_TryGetDragRange`); a drag and the keys stop at the soft ends, typing goes to the hard ones, and a value typed past the soft range widens
it to take the value in (`UIValueSettings_GetDragRangeAround`), so a drag from there starts where the value is. A step snaps to whole steps from the lower
limit (from 0 without one), the upper limit counting as a step so it stays reachable; whole numbers with no step step by 1. **A value is never changed by
showing it:** code's values are only clamped and snapped (`UIValueSettings_Normalize`), conversions only read them (`UIValueSettings_ToShare`), and its text
is the shortest that reads back to the same double (`UIValue_WriteShortestText`; `UIValue_WriteFixedText` for a precision, `UIValue_ParseText` for hostile
text: spaces and a unit around the number passed over, a period for the point, no NaN or infinity, at most `UI_VALUE_MAX_TEXT_BYTES`). Its elements post
through the change helpers (`UIValueCapability_BeginChange`, `_MoveTo`, `_EndChange`, `_CancelChange`, `_ApplyWholeChange`): `Changed` while it moves,
`Committed` once a change is done (not when it ended as it began), `Cancelled` when its start comes back, with the mixed state it began with. Several values
made one is a change too, even to the number the piece held (the first object's): a reset, a paste or a move to it ends the mixed state with `Changed` and
`Committed`. Code that sets the value or the mixed state during a change is where that change now starts (a cancel goes back to code's value, a release
commits only what came after: code posts no events); code's settings keep the change's start, inside them. Copy puts the shortest text on the clipboard,
paste reads a number from it as a whole change (`UIValueCapability_Copy`, `_Paste`), and `UIValue_AttachContextMenu` gives an element Reset, Copy and Paste
(Paste with the unit the element tells through `UIValueHooks.GetUnit`; an item that cannot act is disabled, and one chosen after the element was disabled
meanwhile does nothing; nothing opens during a change: the menu would take the focus and end it). A piece can post under another widget's id, with an index
or not (`UIValueCapability_SetEventTarget`: a compound's parts, a row's fields), and that widget's hook hears each move (`UIValueHooks.OnPartChanged`).

**Slider** (`UISlider.h`). A value piece on a track (`UISliderOptions.Value`, which needs both ends of a drag range), across or upright
(`UISliderOrientation`, `UISlider_SetOrientation`: up is higher, the thumb travelling from the bottom). It is an **edit element**
([UIFramework.md](UIFramework.md), "Entering elements"): moving the focus onto it changes nothing, and Tab and the arrows go past it until it is entered,
by Enter, any press on it or code. A left press on the track brings the thumb under the pointer; a press on the thumb holds it where it was grabbed, so a
click on it changes nothing. Dragging follows the pointer wherever it goes, past the box and past the window: past an end the value is that end. A drag runs
on the drag range around the value where it began, so a value past the soft range (typed, or set by code) moves on from where it is and never jumps into
the range. **Shift is a flag on the rate** (`UISliderDrag`, the one small function `UISliderDrag_Move`): held, the pointer moves the thumb at the
`FineFactor` of the rate; at each change of Shift the thumb's offset from the pointer is kept as it is, so the value never jumps; a thumb held at an end
makes the offset give way (turning back moves it at once), and once the pointer is past that end of the box too the thumb is under the pointer again.
The thumb's share builds up unsnapped, so a slow drag over a stepped track still moves it. Entered, all four arrows are
its own: `ui_up` and `ui_right` step higher, `ui_down` and `ui_left` lower, with Shift a fine step (still a whole step on a stepped track; a list goes from
value to value); repeats follow, at an end they do nothing and are still consumed; Home and End go to the track's ends. Each press is a whole change,
committed when its key comes up. Escape during a held key or a drag cancels that change and the slider stays entered; otherwise it leaves Escape to the
screen, which leaves it, and the presses already committed stay. Left any way (Enter, Tab, focus moving, code), it ends a drag or a held key's change there,
with its commit (a cancel by code takes it back), and what is left of that drag or key does nothing. The wheel is the slider's only while it is entered or with
Ctrl (Shift for a fine step): otherwise it goes on up the tree, so scrolling a panel never changes a value by accident. A middle press let go on it, or the
reset action (`tbp:ui_reset`, Backspace), focused or only hovered, entered or not, puts the default back, a whole change; during a change it does nothing.
Copy and paste (`tbp:ui_copy`, `tbp:ui_paste`) work focused or hovered, and its context menu offers Reset, Copy and Paste. A value set by code keeps its
place (showing it through a log or a power conversion never moves it) and is where a drag in progress goes on from.

**Property box** (`UIPropertyBox.h`, kind `property_box`, `UIKit_CreatePropertyBox`). Blender's field: a value in a box, typed in, or scrubbed sideways if it
is a number (`UIPropertyBoxOptions_Create`, `_CreateRange`, which shows a fill bar), or a string (`_CreateString`: no value piece, no scrub).
- **Showing:** a number at `Precision` digits after the point (none for whole numbers; at most `UI_VALUE_MAX_PRECISION`) with its unit after it
  (`UIPropertyBox_SetPrecision`, `_SetUnit`), a hyphen while mixed (every font has one); a fill bar behind it by the conversion while it has a drag range
  and `ShowsFill` (`_SetShowsFill`; none while mixed, none on a string); a string's text. Showing only reads the value; what it shows changing is
  measured again, so a box sized by its content follows its number.
- **Scrubbing** (a number): a left press holds; once the pointer has gone `UI_PROPERTY_BOX_SCRUB_THRESHOLD` UI pixels sideways it scrubs, asking for a
  hidden-cursor drag (`UIScreen_CapturePointer`, so the window's edges do not stop it, and the cursor comes back where it hid: past the threshold, a
  few pixels from the press). With a drag range the
  box's width spans it through the conversion; without one it moves by `UIValueSettings_GetScrubRate` per UI pixel (a step every four pixels, else the
  value's magnitude every hundred). Shift slows it by the `FineFactor`, Ctrl snaps it coarsely (`UIValueSettings_SnapCoarse`: a power of ten for the span,
  never finer than the step; tenths of the share on a conversion that is not linear). The rate and the coarse snap count by the value the scrub began at,
  and the scrub's place keeps to the range and the hard limits, so turning back past an end moves the value at once. `Changed` as it moves, `Committed`
  at the release; Escape cancels it (`Cancelled`), and so does a right press during the left one, scrubbing or not (that right press opens no menu). A
  scrub does not enter the box: while it holds, the box switches entering off (`Widget_SetNeedsEntering`) so the focused box hears every key first
  (Escape cancels, the rest does nothing), and back on as it ends, however it ends.
- **Typing** is its entered state, in a text box of its own: its look base holds a text box's capability, whose keys, mouse, clipboard and undo (one per
  visit: the value's history is the caller's) it types with, hearing the box's edits and the visit's end through a host (`UITextBoxHost`) in place of the box's
  events. It enters itself: a click without moving, Enter or a double click on a number, any press on a string, everything selected; a number's text is the
  shortest that reads back to its value, and several values (mixed) type from an empty text, so leaving it empty changes nothing and any number typed makes
  them one. A number's text holds at most `UI_VALUE_MAX_TEXT_BYTES`, and Insert does not overwrite. While the text reads as no number the box shows the
  invalid state. Enter keeps (a good number clamped into the hard limits and snapped becomes the value, a bad one goes back with the Refused cue), Escape
  cancels, Tab and Shift+Tab keep and go on, typing in the next box at once. A visit that
  changed the value ends in `Changed` then `Committed`; text left as it began changes nothing, so a far double (1e7 + 0.1) or a float's value is kept exactly.
  A string's visit posts the same with its length; its text is read by id (`UIPropertyBox_GetText`) and set by code (`_SetText`, `_SetTextLimits`,
  `_SetDefaultText`).
- **Also:** a middle click let go on it or the reset action puts the default back (with none, the reset action is not the box's and goes on); copy and paste
  work focused or hovered (a number's shortest text, a number read from the clipboard with its unit passed over); its context menu is Reset, Copy and Paste
  (Paste with its unit too), and while typing a text box's. Ctrl and the wheel step a number, not during a press; a plain wheel goes up the tree. Disabled
  (`UIPropertyBox_SetIsEnabled`, or `UIValue_SetIsEnabled` on a number), it takes a scrub back, keeps a visit and takes no focus.

**Labelled element and form** (`UILabelled.h`, kinds `labelled` and `form`). `UIKit_CreateLabelled` makes a label and calling code adds the element it
labels to it (a dropdown, a toggle, a text box, a property box, anything): the element keeps its own id and functions, the labelled one places the two,
across (the label at the left, vertically centred, the element filling the rest) or stacked (the label above, `UILabelled_SetLayout`), with the theme's
`spacing`. Its label column is as wide as its label, as a width of its own (`UILabelled_SetLabelWidth`: a longer label runs past it, neither cut nor
wrapped), or, while it is a form's row, as the form says (taken out of the form, its own again). `UIKit_CreateForm` makes a column of rows, each as tall as
it measures and as wide as the form: its labelled rows across that show and have no width of their own share the widest of their labels (each at its row's
scale), so the rows line up. Hover text set on a labelled element shows over its label and over an element with none of its own.

**Compound slider** (`UICompoundSlider.h`, kind `compound_slider`, `UIKit_CreateCompoundSlider`). A name, a slider and a property box (each of the look the
theme names for its kind, reachable by `UICompoundSlider_GetPartIds`) sharing one value: across in a row (the box `box_width` wide beside the slider),
upright in a column (the box at the bottom). Options and `UICompoundSlider_SetShows` choose which show; with no slider across, the box takes the room. It
answers the value piece itself: `UIValue_*` on it moves both parts, settings included; setting either part's value, by its id or by the pointer and the
keys, moves the other and the compound's own, and settings set on a part by its id are the compound's (both parts get them). Its parts' `Changed`,
`Committed` and `Cancelled` are posted under the compound's id, one of each per change, never under the parts'; their other events (pressed, released) stay
their own, so listening on the compound's id hears its value events alone. It scales as one and its parts' hover text falls back to its own.

**Row of fields** (`UIVectorRow.h`, kind `vector_row`, `UIKit_CreateVectorRow`). Two to four property boxes for a vector's components, each after an axis
tag (X, Y, Z, W) in the theme's colours, sharing the row's width evenly (in a row too narrow for its tags, each tag keeps to its own slot). Each field is
reachable by its own id (`UIVectorRow_GetFieldId`) for its own settings and mixed state (`UIVectorRow_SetMixed`, `_SetValue`, `_GetValue` for short). Its
fields' value events are posted under the row's id with the axis as the event's index (`UIEvent.HasIndex`, `UIEvent.Index`), so a handler writes only that
component, what several objects with different components need. Tab goes from field to field, typing in each at once. Dragging down across several fields to
edit them together is not built.

**Scroll area** (`UIScrollArea.h`, kind `scroll_area`). Content larger than its box. Calling code adds the content to the area as to any parent
(one child is the usual, a column stack); every child but its two bars gets the same box. It measures the content at the view's width while only
the vertical axis scrolls, unbounded across while that axis scrolls, and the content box is at least the view, so a short page still fills it. It
cuts its subtree to its box and places the content at its offset; its two bars (made with it by `UIKit_CreateScrollArea`, each of the look the theme
names for the `scrollbar` kind) lie on top along its right and bottom edges, by the modes (`UIScrollMode_Disabled`; `_Auto`, a bar while the content
is longer; `_Always`): a bar that does not show has no size, and the vertical one runs the whole height, the corner being the end of its track. The
wheel over it, or over anything inside it that leaves the wheel alone, scrolls it while it can still scroll that way and then goes on up the tree, so
an area inside an area scrolls the outer one at the inner's end; Shift turns a plain wheel across. Page Up and Page Down that the focused widget inside
leaves scroll a page. A widget inside focused by the keyboard or by code is scrolled into view, as little as it takes (nested areas each in the same
tick); a press never scrolls, so the content does not jump under the pointer. It takes input and is not focusable. Measuring is cached: an idle area
measures nothing.

The **scrollable piece** (`UIScrollable.h`) is what every element whose content can be larger than its box holds (the scroll area, the dropdown's
list, the text box; later the list, the windows): the content's and the view's sizes in its own UI pixels, the offset (clamped to [0, content - view]
whenever either changes) and a mode per axis (`UIScrollMode_Hidden` scrolls with no bar: a one-line text box across). Calling code moves it by id: `UIScrollable_SetOffset`, `_ScrollBy`, `_EnsureVisible` (a box of the
content), `_RevealWidget` (a descendant), `_GetOffset`, `_GetRange`, `_GetSizes`, `_SetModes`, `_SetWheelStep`. The arithmetic on the struct is pure.
Offsets are floats: content past about 16 million UI pixels scrolls in steps larger than a pixel.

**Scroll bar** (`UIScrollbar.h`, kind `scrollbar`). A track and a thumb along one axis, bound to an element holding a scrollable piece
(`UIScrollbarOptions.TargetId`, `UIScrollbar_SetTarget`). The thumb's length is the view's share of the content, never shorter than the look's
minimum (`min_thumb_length`); a left press on the thumb holds it where it was grabbed and a drag moves it, past the window too; a press on the track
pages toward the pointer, and holding it pages again after `UI_SCROLLBAR_PAGE_DELAY_SECONDS` until the thumb reaches the pointer. The wheel over it
scrolls along its axis. With nothing to scroll the thumb fills the track and presses do nothing. It is not focusable. An element holding bars places them
each layout with `UIScrollbar_MeasureThickness` and `UIScrollbar_PlaceAlongEdge` (inside a border, for a text box).

**Dropdown** (`UIDropdown.h`, kind `dropdown`). One choice of several, through the **choice piece** (`UIChoice.h`): options as a list of texts
(`UIChoice_SetOptions`) or from providers, a count and the text at an index (`UIChoice_SetProvider`, for long or changing lists; the rows in view
keep their texts until the count changes or `UIChoice_Refresh` says the data did), the selected one
(`UIChoice_SetSelected`, `_GetSelected`) and mixed (`UIChoice_SetMixed`: the head shows the mixed text, a hyphen unless its options give one). The
**head** is a press element: focused, the arrows go past it; a left press opens the list at once (so press, drag to a row, let go chooses it; a
release within a click's distance of the press chooses nothing, so a list placed over its head is not chosen by the click that opened it), Enter
or Space opens it too; Ctrl and the wheel step through the options while it is closed, a whole change a notch, and a plain wheel goes on up the tree.
The **list** is a popup below the head, or above when there is too little room below and more above (`UIDropdownDirection_Auto`; `Up` and `Down`
force a side), as wide as the head at least, at most `VisibleCount` rows tall (`UIDropdown_SetVisibleCount`) and shorter when the room is. It makes
widgets for the rows in view only (the **row recycler**, `UIRowRecycler.h`: bound in the list's Update, placed in its Layout, reused as it
scrolls), so a provider's hundred thousand options cost what the rows on screen cost. With more rows than fit it scrolls by the wheel, the keys
and a bar at its side, between strips at its top and bottom whose arrows show while more lies that way (a press on one scrolls a row, holding it goes
on). It is an edit element entered as it opens: Up and Down move the highlight, Home and End go to the ends, Page Up and Page Down a list's height, a
letter to the next option starting with it, Enter chooses, Tab and Escape close it. The highlight starts at the selected option and follows the
pointer when it moves (rows scrolled under a pointer at rest leave it where the keys put it). A row chosen posts `Changed` then `Committed` on the
head with the index as a double (nothing when it was selected already and not mixed), and the list closes, the focus back on the head. Disabled (`UIDropdown_SetIsEnabled`), the head swallows presses and is not focusable.
`UIDropdown_Open`, `_Close` and `_IsOpen` work by id.

**Menus** (`UIMenu.h`). A menu is data: `UIMenuItem`s, each a label with an optional icon, a shortcut text (only text: nothing binds it), a check,
disabled, a separator, or a submenu (`Children`, or built by the menu's `SubmenuProvider` when it opens, `HasProvidedSubmenu`, through
`UIMenuBuilder_Add`, which copies each item at once). Submenus nest at most `UI_MENU_MAX_DEPTH` levels; one that cannot open does not, and is
logged once. `UIMenu_Open(screen, items, count, &options, &menuId)` copies the whole tree and opens its first level as a popup against an
anchor; `UIMenu_OpenAt(screen, request, items, count, handler, userData)` opens one for a context request at its place, owned by the widget it is for.
Items are press elements: the arrows move between them (navigation, kept in the topmost menu), Enter or Space activates, a click or a release over
one of a press that began elsewhere activates. An item with a submenu opens it instead: after the pointer rests on it for
`UI_MENU_SUBMENU_DELAY_SECONDS` (resting on another item of the same menu closes it again), on a click, or on Right (its first item focused); Left
closes a submenu, the focus back on its item; Escape closes one level. A menu opened by the mouse has nothing focused; one opened by a key focuses
its first item. Activating an item closes every level, the focus goes back where it was, and the menu's handler gets a `UIMenuActivation` (the
item's id, the owner, the button and modifiers). The menu knows nothing of actions: whoever opens it runs what an id means.

**Menu bar** (`UIMenuBar.h`, kind `menu_bar`). A strip of entries (`UIMenuBar_AddEntry`: a label and its menu's items, copied), each a press
element: a left press opens its menu below it at once (another press on it closes it), so press, drag to an item and let go activates it; Enter or
Space opens it with its first item focused. While a menu is open, the pointer coming onto another entry switches to its menu, and in the open menu
Left and Right with nothing more to open move to the entries beside. A press on the bar counts as inside its menus. Activated items call the bar's
handler (`UIMenuBarOptions.Handler`, `UIMenuBar_SetHandler`). Alt alone does nothing yet.

**Status bar** (`UIStatusBar.h`, kind `status_bar`). A strip with left, centre and right sections, each a row stack made with it by
`UIKit_CreateStatusBar` (`UIStatusBar_GetSection` for calling code to add to): the left and right ones as wide as what they hold, the centre one
filling what is between; the bar is as tall as a line of text or as what its sections hold, whichever is more. Its line of text, at the start
of the centre section and cut where the right one begins, is a message
(`UIStatusBar_SetMessage(screen, id, text, seconds)`, 0 seconds for until it is replaced or cleared), else the hover text where the pointer rests
(`UITooltip_GetCurrent`, before the delay too) while it shows hover text (`UIStatusBar_SetShowsHoverText`). For now it is placed like any element.

**Text box** (`UITextBox.h`, kind `text_box`, `UIKit_CreateTextBox`). Text to type in, or to read and select, on one line
(`UITextBoxOptions_Create`) or several (`UITextBoxOptions_CreateMultiline`). The text and its rules are the **text piece** (`UITextEdit.h`, pure): limits
judged on the result (`MaxBytes`, 64 KiB by default and `UI_TEXT_EDIT_MAX_BYTES` at most; `MaxCharacters`, `MaxLines`, `AllowsNewlines`, forbidden
codepoints, a filter callback), incoming text cleaned the same way for typing, pasting and code (invalid UTF-8 as U+FFFD, `\r\n` and `\r` as `\n`,
control characters dropped but the line break and the tab, a line break as a space where none may go, what does not fit cut and refused with the `Refused`
cue), words (letters and digits, spaces, punctuation), and the undo.
- **An edit element:** focused, it hears only Enter and the short list (below), so typing on a focused box does nothing; Enter, a press or code enters
  it. Entered by a key, a one-line box selects everything and a box of several lines puts the caret at the end; by a press the caret goes where it landed;
  by code it stays. Entered, it hears every key first: the arrows (Ctrl by words, Shift extending), Home and End of the line as shown (wrapped lines
  too), Ctrl+Home and Ctrl+End, the page keys, Backspace and Delete (Ctrl by words), Insert for overwrite (a block caret, until the box is left), the
  actions `tbp:ui_copy` (Ctrl+C, Ctrl+Insert), `tbp:ui_cut` (Ctrl+X, Shift+Delete), `tbp:ui_paste` (Ctrl+V, Shift+Insert), `tbp:ui_undo` (Ctrl+Z),
  `tbp:ui_redo` (Ctrl+Y, Ctrl+Shift+Z) and `tbp:ui_select_all` (Ctrl+A), Enter as a line break where line breaks are allowed (refused, with the cue,
  at the line limit) and Tab as a tab with `AcceptsTab`. Before any action it claims every press that types text: a letter alone or with Shift, with
  Ctrl and Alt together, or with the right Alt alone (AltGr as GLFW reports it), so an action bound to a letter types the letter and AltGr's
  characters never select all or paste; the left Alt is a shortcut. It leaves Up, Down and the page keys on a box that shows one line (it holds no
  line break and does not wrap) and Ctrl+Enter to whoever is around it; the screen's rules do the rest: Enter keeps and leaves, Escape leaves (putting
  back the text it had at entering with `EscapeReverts`, on by default for one line and off for several), Tab keeps and carries on into the next box,
  selected.
- **Events:** every edit posts `Changed`; leaving after a visit that changed the text posts `Committed` once; a revert of a changed text posts
  `Cancelled`. They carry the text's length in bytes as a double: the text is read by id (`UITextBox_GetText`). An edit while the box is not entered
  (the short list's paste and reset) is a whole change, `Changed` then `Committed`. Its own context menu does not end a visit: the menu takes the
  focus and gives it back, its item acts within the visit (so a field's visit undo and Escape's revert cover it), and a press elsewhere that closes
  the menu ends the visit there. Code setting the text posts nothing. A filter or validator must not change the box: its edits are refused while
  they run.
- **The mouse:** a press places the caret, a drag selects (past an edge the text scrolls, faster further out), Shift with a press extends, a double
  click selects a word and a triple click a line, and a drag after them goes by words or lines. A right press outside the selection moves the caret
  first; a right click opens the box's context menu (Undo, Redo, Cut, Copy, Paste, Delete, Select all, each disabled when it cannot act), which calling
  code can replace with a provider of its own. The wheel scrolls it and then goes up the tree. The pointer is an I-beam over it.
- **The short list**, focused or hovered and not entered: copy the whole text (refused when masked), paste over the whole text (one change and one undo
  step), reset to the default (`HasDefault`).
- **Undo:** typing groups into one step until a pause (`UI_TEXT_EDIT_DEFAULT_UNDO_PAUSE_SECONDS`), twenty characters, a caret move or another kind of
  edit; a paste, a cut, a word deletion and an edit by code are steps of their own. The list keeps at most `UI_TEXT_EDIT_MAX_UNDO_STEPS` steps within
  its byte budget (256 KiB by default), dropping the oldest; a step larger than the budget empties it. Undo and redo are never refused for space. While
  the box is entered the undo keys are always its own (the `Refused` cue with nothing to undo), never something behind it; not entered, they are not its.
  The list lives as long as the box (`UITextBoxUndoScope_Box`) or starts empty at each visit (`UITextBoxUndoScope_Visit`, for an editor's field whose
  own history records the `Committed`). `UITextBox_SetText` and a change of limits empty it; `UITextBox_ReplaceRange` is undoable.
- **Showing:** a blinking caret a line tall (in an empty box too), a selection in the theme's colour while entered and in its inactive colour
  otherwise, a placeholder while empty, the scroll bars by the modes (inside the box's border; one line scrolls across with no bar), the caret kept in
  view (trailing spaces count). Wrapped, lines break at the view's width and the box scrolls down, and across with no bar only to a word longer than a
  line (words are not broken); not wrapped, both ways. **Masked** (`IsMasked`): one mask character per character (the theme's `mask`), copy
  and cut refused, the whole text one word. **Read-only** (`IsEditable` false): its own box (`read_only_box`), no caret; selectable, it is an edit
  element whose keys scroll it and copy; not selectable, it takes no focus. It can show a whole text component tree (`UITextBox_SetComponent`: translate
  and sprite nodes, a sprite one object character in the selection and left out when copied); such a box changes by a new tree or a text, never a
  range of it (`UITextBox_ReplaceRange` refuses), keeps it whole through new limits, masks it when masked, and made editable keeps its text without
  the sprites. Disabled, it swallows presses and takes no focus.
- **Style ranges** (`UITextBox_SetStyleRanges`, at most `UI_TEXT_BOX_MAX_STYLE_RANGES`): byte ranges with a `TextStyle` (a colour, a font, a size
  relative to the box's text, a shadow, underline, strikethrough, spacing), later ones over earlier ones, moved by every edit, undo and redo (an
  insertion inside a range grows it, one at its end does not); an edit that empties a range removes it, and undo does not put back an end an edit cut
  (code that colours a text sets its ranges again after a change). A style a string component would refuse (a size or a spacing out of bounds) is
  refused when set. A masked box ignores them.
- **Cost:** a keystroke builds the document and the layout again, in time proportional to the text (about 5 ms at 64 KiB in an optimized build) and
  to the style ranges over each piece of it (disjoint ranges little, deeply nested ones the square of their count); an idle box costs nothing, and
  drawing skips what lies out of view (it still steps over the rest: a tenth of a millisecond at 64 KiB). That is what the default `MaxBytes` is set
  from. A paste reads no more of the clipboard than the limits could take (`UITextEdit_GetMostIncomingBytes`).
- **Not built:** formatting the user chooses and that is stored (colours, fonts and sizes picked on a selection, images inline, saved as text
  components: a paper editor); the user types plain text and style ranges come from code. Nor input methods (IME composition), right-to-left text and
  shaping, middle-click paste, dragging selected text, breaking a word longer than a line, or incremental layout, which a far larger text (a script
  editor) needs. Reading another program's clipboard goes through GLFW, which waits for that program to answer: one that never does stalls the
  game during the paste (X11).

**List** (`UIList.h`, kind `list`, `UIKit_CreateList`). Rows of the caller's data, at a fixed row height (the theme's `row_height`), through two providers: a
count, and what a row shows (`UIListRowData`: a text, an icon, dimmed, up to `UI_LIST_MAX_CELLS` toggle cells' states). Only the rows in view have widgets,
reused as the view moves (the row recycler), so a hundred thousand rows cost what the rows on screen cost: memory and time follow the view, never the data,
and an idle list allocates nothing. It reads its count when it is made, so code can select and reveal rows at once. Calling code says what changed:
`UIList_Refresh` (rows show something else), `UIList_InsertRows` and `UIList_RemoveRows`
(the selection, the current row and the anchor move with their rows; a removed selected row drops out), `UIList_SetProvider` (other data altogether); a count
that changed with no word is taken as it is.
- **Selection** by data index (`UISelectionMode`: none, one, several), kept as sorted runs (`UIListRanges_*`, pure): a click selects only its row, Ctrl toggles
  it, Shift selects from the anchor (the last row clicked without Shift), Ctrl and Shift add that range; a press on a selected row of several waits for its
  release, so the press can drag them; a right press keeps a selection it lands in; a click on no row clears it. Each user change is `Changed` then `Committed`
  with the number selected; code's (`UIList_SetSelected`, `_SetRangeSelected`, `_ClearSelection`) post nothing. `UIList_GetSelection` and
  `UIList_GetSelectionRange` read the runs.
- **Keys**: an edit element. Focused, it takes Enter (which enters it) and nothing else of its own; a press on it enters it too. Entered: Up and Down move
  the current row (the keyboard's cursor, outlined while it has the focus) and select the row they reach (Page Up and Down by a view, Home and End to the
  ends), with Shift selecting from the anchor and with Ctrl moving only the current row; Space selects the current row (Ctrl+Space toggles it), the
  select-all action every row; Enter opens the current row (`Activated`); Escape and Tab leave.
- **Events** carry the row as a `UInt64` value (`UIPropertyType_UInt64`, the data index): `Activated` (Enter, a double click), `DoubleClicked`,
  `ContextRequested` (a right click on a row, or the menu key on the current row; `UIList_GetItemAt` and `UIList_GetCurrent` for a menu provider), and
  `CellToggled` (its index the cell): a cell's state is the caller's data, which the caller flips and refreshes.
- **Drags**: made a drag source (`UIListOptions.IsDragSource` and `DragKind`, `UIList_SetDragSource`), a press on a selected row moved past the drag distance
  drags the selection (a press on a toggle cell, or a tree's expander, never drags), the payload's value the pressed row, its text the row's or how many
  go. Made a target (`UIDragDrop_SetTarget`), it draws a line
  where a drop would go while it would take it, scrolls when the pointer nears its top or bottom, and `UIList_GetDropIndex` says where a dropped point
  falls. While a drag is on, no row looks hovered.
- The wheel scrolls it and then goes on up; its bar is a `scrollbar`. Disabled, it swallows presses, takes no focus and dims its rows.

**Tree** (`UITree.h`, kind `tree`, `UIKit_CreateTree`). Nodes that live with the caller, named by 64-bit ids, through a provider (`UITreeProvider`, every hook
required): a node's children (the root's with `isRoot`), whether one is expanded and setting it (the caller keeps that state, so two trees over the same
data can differ, or agree), what a node shows (as a list's row), and whether a node still exists. The tree walks the expanded nodes into rows (at most
`UI_TREE_MAX_ROWS`, `UI_TREE_MAX_DEPTH` deep) when told the data changed (`UITree_Refresh`) or at once when the user folds one, and shows them as a list
does, each row's content indented by its depth after its expander. Selection is by node and survives folding; `UITree_Refresh` drops nodes that are gone.
Folding a node with the current row inside moves the current row to it. The keys are the list's, and Left folds the current node (or goes to its parent),
Right unfolds it (or goes to its first child); a click on an expander folds or unfolds. Events carry the node as a `UInt64` value. A drag source as a list is
(its payload's value the pressed node); as a target, `UITree_GetDropPlace` says above, below or into which row a point falls, shown by the line or a box
around the row.

**Modal dialog** (`UIDialog.h`, kind `dialog`, `UIKit_CreateDialog`). A title (a label of the `heading` variant), a message that wraps at the theme's
`max_width`, a column for content calling code fills (`UIDialog_GetContentId`), and buttons from a list (`UIDialogButton`: an answer id, a text, at most one
default and one cancel). It is made shut and opened once (`UIDialog_Open`, centred in the popup layer over a backdrop that takes every press), with the
focus on a field given (entered at once) or on the default button when it can take the focus, else the first button that can; closed, it is
destroyed, so each question makes a new one. A button's activation answers
with its id: `Activated` under the dialog's id, the answer its index. **Enter answers with the default and Escape with the cancel in one press, even from
inside an entered field**: the dialog is the field's ancestor, so it hears what the field leaves unused, and leaves the field itself first (Enter keeping,
Escape cancelling). Enter on a field only focused enters it (the screen takes that press first). It closes at the first update after its answer was
delivered, so a handler reads its fields by id, and closes alone (`UIScreen_ClosePopupAndItsOwn`): a dialog its answer opened stays. With no cancel button,
Escape is swallowed; a disabled default or cancel button is as none (a rename's OK disabled until a name is typed answers nothing). While topmost it keeps
the focus inside it, on a button that can take it; it closes unanswered when its owner goes or by `UIDialog_Close`.

**Colour picker** (`UIColorPicker.h`, kind `color_picker`, `UIKit_CreateColorPicker`). A swatch (a press element: its click, Enter or Space opens the popup)
showing a `Color` or a `RenderColor` (`UIColorKind`: four channels, or a tint, a brightness and an opacity) over a checkerboard where it is not opaque, a
hyphen while several colours differ (`UIColorPicker_SetMixed`). The popup (below the swatch, above with no room) has a square of saturation (across) and
value (up), a hue strip, an alpha strip with `HasAlpha`, a property box per channel and a hex text box ("#rrggbb" or "#rrggbbaa", applied as typed, put
back if it reads as no colour when left); it opens with the hex field entered. The square and the strips are edit elements: drags, and the arrows by 0.01
(0.001 with Shift) once entered. They are drawn from quads with a colour at each corner (`UIRenderContext_RenderPixelGradient`), the square from
`UI_COLOR_PICKER_SQUARE_CELLS` across and down: no shader of its own. `Changed` comes with each move, `Committed` when the popup closes with another colour
than it opened with, and Escape puts the first colour back (`Cancelled`) and closes it, in one press from an entered field. The conversions
(`UIColorHsv_FromRgb`, `UIColorHsv_ToRgb`, `UIColor_ToHsv`, `UIColor_FromHsv`, `UIColor_TryParseHex`, `UIColor_WriteHex`) are pure and round-trip every
`Color`; saturation or value at 0 keeps the hue, so a grey can be dragged back into colour.

**Tool bar** (`UIToolbar.h`, kind `toolbar`, `UIKit_CreateToolbar`). A strip of buttons from a list (`UIToolbarItem`: buttons, toggles, choices of a group,
separators), across or upright (`UIToolbar_SetDirection`); the editor makes its bars from its actions. Each item but a separator is a kit button of the
variant its options name (`tool` by default) with the item's icon, a text if it has one (an action with no icon yet), and its name as hover text. The bar
posts under its own id, the item's id as the index: `Activated` for a button, `Changed` then `Committed` with the new state for a toggle or a choice (a
choice taken lets go of the others of its group; a click on the one taken posts nothing). `UIToolbar_SetIsToggled`, `_SetIsEnabled` and `_GetButtonId` work
by item id, and code's changes post nothing. Each button is at least the theme's `button_size`, as wide across as the widest; a separator takes
`separator_spacing` with its line in the middle. A bar shorter than its items cuts the last ones off: neither drawn nor hit past its end.

**Drag and drop** is a service of the screen ([UIFramework.md](UIFramework.md), "Drag and drop"): a list and a tree are sources and targets through it, and
any widget can be one.

**Context menus** are a service of the screen ([UIFramework.md](UIFramework.md), "Context menus"): a provider set on a widget answers a right click or
the menu key on it or on anything inside it that has no provider of its own, and opens a menu with `UIMenu_OpenAt`.

## Windows

**Window** (`UIWindow.h`, kind `window`, `UIKit_CreateWindow`). An icon, a title, a frame that moves and resizes, minimize, maximize and close,
and content calling code fills. It works on any screen: the game window's, or a screen in the world, where its title bar and edges follow the
ray on the picture as any held press does.
- **Made whole** as a root in the screen's `Floating` layer, in front of the floating windows: its frame (a widget of the kit's own under the
  rest, as large as the window), its title bar (another), three caption buttons in it (kit buttons of the theme's `window_button` and `window_close` variants, with placeholder letters until icon art exists),
  eight edges and corners (widgets of the kit's own, each with its pointer's shape) and its content: a column in a scroll area of the theme's
  `window` variant (drawn over the window's content background). The creation gives
  the column's id (`UIWindow_GetContentId` later): calling code adds its elements there and talks to the window by its id, never to its frame.
- **Its box.** Its floating box (`UIWindowOptions.Position` and `Size`, centred on the workspace without a position; `UIWindow_SetBox`,
  `UIWindow_GetBox`) is in the screen's UI pixels, a root's placement units, and is what the window goes back to. What shows
  (`UIWindow_GetShownBox`) is that box grown to the window's least size and kept reachable on the workspace: the title bar's whole height on it,
  and at least `UI_WINDOW_REACHABLE_WIDTH` (in the window's own UI pixels) of the title bar's draggable part, the title bar less its buttons
  (`UIWindow_KeepReachable`, pure). Keeping it reachable never changes the floating box, so a game window that shrinks and grows back puts every
  window back where it was. Each update places it again, so a move, a resize, a new surface size or new workspace strips all count.
- **Moving and resizing.** A left press on the title bar moves it, following the held press past the surface; the drag starts from where it
  shows. An edge or a corner resizes it (`UIWindow_ResizeBox`, pure): each edge it names stops at the workspace's edge (or where it began, when that
  lies beyond) and where the least size is left, and a top edge a title bar's height above the workspace's bottom. Either keeps the floating box
  reachable too. Other buttons move and resize nothing (their press and release during a drag leave it going); minimizing, maximizing or a lost
  press (the window hidden) end it. A double click on the title bar maximizes, or restores a maximized or minimized window. A maximized window is
  moved and resized by nothing; one that is not resizable (`IsResizable`, `UIWindow_SetIsResizable`), minimized or maximized shows no edges.
- **Its least size,** in its own UI pixels (its scale multiplies it), is the largest of the theme's `min_size`, its own (`MinimumSize`,
  `UIWindow_SetMinimumSize`), what its title bar needs, and, per axis that fits (`UIWindowFit_Fit`: `FitAcross`, `FitDown`, `UIWindow_SetFit`), what
  its content needs: across, the content measured with no width at all (as narrow as it can be: a wrapped label at its longest word), plus the
  vertical bar's room while down scrolls; down, the content's height at the window's width. An axis that scrolls (`UIWindowFit_Scroll`) gives the
  content its preferred size, with a bar while that is larger. The default fits across and scrolls down. The window's Measure is its least size
  (`UIWindow_Measure`), cached as any element's until something in it changes.
- **Minimized** (`UIWindow_Minimize`, its first button): its content is hidden (the focus in it is lost; its state is kept) and its frame is only
  a title bar `minimized_width` wide where the window was: the icon, as much of the title as fits, restore (the same button) and close. It can be
  moved. Restoring (`UIWindow_Restore`) gives the same size back; restored by its button or a double click, it gives the focus back inside
  too (the press gave it to the window itself while its content was hidden). It is as wide as `minimized_width`, or what its title bar needs
  when that is more.
- **Maximized** (`UIWindow_Maximize`, its second button, a double click on the title bar): it stands in the `Maximized` layer, over every floating
  window, and fills the workspace; restoring brings back its floating box, in front of the floating windows. One window of a screen shows maximized
  at a time: maximizing one restores any other. A maximized window that is minimized is a floating title bar, and restores to maximized
  (restoring the other one then). Giving a floating window the focus by code (`UIWindow_Focus`, opening a type) restores a window maximized
  over it, so the one focused shows. Only a root minimizes or fills the workspace: a window another widget places shows neither button, and
  `UIWindow_Minimize` and `UIWindow_Maximize` refuse it.
- **Closing** (its close button while `CanClose`, `UIWindow_Close`) asks its close query first (`CloseQuery`, `UIWindow_SetCloseQuery`): false keeps
  it, and the query may open a dialog and close it later with `UIWindow_CloseNow`, which does not ask (a query that closes it at once counts as
  closing it, whatever it answers). A closed window is destroyed. Which caption buttons show is `CanMinimize`, `CanMaximize` and `CanClose`
  (`UIWindow_SetButtons`), and a minimized window always shows the one that restores it; code can minimize and maximize any root window and close
  any window. It posts the `Open` cue when it is made and `Close` when it closes.
- **Focus.** A press anywhere in it raises it within its layer, as a press raises any root. The window is a focusable group: navigation reaches it
  as one, and Enter goes in. A press on its title bar, a caption button, an edge or its border acts on nothing inside: those parts keep the focus
  where it is on a press (`Widget_SetKeepsFocusOnPress`, [UIFramework.md](UIFramework.md)), so an element being edited inside stays entered in the
  same visit, and the window then takes the focus itself unless it is inside already: from elsewhere or from the window itself, back to what last
  had it inside, entered again if it was (on a screen that has the keyboard), else to its default target (`UIWindow_SetDefaultFocus`, entered if
  asked: a world view), else the window itself. A double click on the title bar acts when its second press is let go, so the release lands on
  the title bar, never on what the window shows there afterwards. `UIWindow_Focus` does the same from code and brings
  it to the front. It is active (`UIWindow_IsActive`) while the focus is in it, or in a popup whose owner is in it (its dropdown's list, a menu it
  opened, a popup opened from those: `UIScreen_GetPopupOwner`); its look shows the `focused` state then. What had the focus in a popup is not
  remembered as its own: a press on its title bar afterwards goes back to what had it inside before.
- **The workspace** is the surface less the strips the screen's workspace insets name (`UIScreen_SetWorkspaceInsets`, `UIScreen_GetWorkspaceBox`,
  [UIFramework.md](UIFramework.md)): the bars' room, which whoever lays the bars out sets. None by default.
- **Its look** (a struct starting with `UIWindowLookBase`) draws the frame, the title bar (the icon, then the title cut where the buttons begin:
  `UIWindowCapability_GetTitleEnd`), the content's background and an edge's highlight while the pointer is on it or it is dragged
  (`UIWindow_GetEdgeState`: a hidden edge is normal with an empty box); it tells the behaviour the theme's metrics with the icon's width
  (`UIWindowCapability_SetMetrics`) and measures through `UIWindow_Measure`.
- **Not built:** the content column's padding and spacing are read from the theme when the window is made (a theme swap changes the frame at once,
  not those); the caption buttons take no focus by the keys and there are no window keys; a window posts no events of its own for moves, resizes
  or its states (code reads `UIWindow_GetBox` and `UIWindow_GetState`); dragging a maximized window's title bar does not restore it.

**Window types** (`UIWindowTypes.h`). The windows a UI opens by key (`tbp:editor_outliner`): each registered with a display name, an icon, a
function that fills a new window's content (`UIWindowTypeFill`), its windows' fits, default size and least size, and whether only one may be open on
a screen (`IsSingle`). `UIWindowTypes` keeps them in a `Registry` under `tbp:ui_window_type` ([Registry.md](Registry.md)), added to the program's
root registry; `main.c` makes the program's one and `Services.WindowTypes` carries it. A frame registers the types it offers under a source key of
its own while it runs and withdraws them together (`UIWindowTypes_WithdrawSource`) when it goes.
- `UIWindowTypes_Open(types, screen, key, &windowId)`, or `_OpenAt` by index: a single type's window that is open on the screen already (found
  anywhere in its tree: `UIWindowTypes_FindOpen`) is restored if it is minimized, brought to the front and given the focus. Otherwise a new window
  is made where the type's last window was when it closed, or at its default size centred on the workspace (either stepped down and to the right
  by `UI_WINDOW_TYPES_CASCADE_STEP` while another window's top-left stands there), filled, and given the focus (`UIWindow_Focus`: what the fill
  focused inside keeps it, else the default focus target, else the window). A fill that fails takes its window with it; one that closes its own
  window leaves the open successful with no window.
- A window that closes leaves its floating box with its type (`UIWindowTypes_GetRememberedBox`): the record is the type's, whatever the screen.
- `UIWindowTypes_WriteMenuItems(types, firstId, items, capacity, &count)` writes a Window menu: an item per active type with its name and icon, its
  id the type's index after `firstId`, so the menu's handler opens `id - firstId`.

### Click modes

Button and toggle options have `ClickMode` and `ActivationButtons` (`UIButton_SetClickMode`, `UIButton_SetActivationButtons`, and the toggle's).

| `UIClickMode` | Activates at | So |
|---|---|---|
| `OnPressAndRelease` (the default) | its own press let go on it, nothing covering it there | a slip forgives: press, drag off, let go, and nothing happens |
| `OnPress` | the press | at once, and what follows does nothing |
| `OnRelease` | a release on it, wherever on this screen the press began, a press handed to it too | press-drag-release menus |

`ActivationButtons` is a mask of mouse buttons, `UI_BUTTON_BIT(GameKey_MouseMiddle)` and so on, up to `UI_BUTTONS_ALL`; `UI_BUTTONS_LEFT` is the default. Each button is held
on its own, and another button's press does nothing to it. A press that is lost (`OnPressLost`) activates nothing, and a release ends what its press held even if its button stopped
activating meanwhile. An element's own type wires `UIPressable_OnMouseInput`, `UIPressable_OnPressLost`, `UIPressable_OnKeyboardInput` and `UIPressable_Update` as the standard ones do;
one that leaves out the loss still lets go at its next update once the button is up.

## Events

Elements post through `UIScreen_PostWidgetEvent`, or `UIScreen_PostWidgetMouseEvent` when there is mouse data, delivered at the end of the update, never in the middle of routing.
`UIEvent.WidgetEventId` is a `UIElementEvent`:

| Event | Posted | Carries |
|---|---|---|
| `Activated` | a button's click, by its click mode, or its key | mouse data |
| `Changed` | the value moved and may go on: during a drag or a scrub, at each key step; a dropdown's choice (with `Committed`); a text box's edit; a property box's typed value (with `Committed`) | bool or double (a value's value, a dropdown's index, a text box's or a string property box's length in bytes) |
| `Committed` | the change is done: the release, the key's release, the press lost, a text box left after a visit that changed it; what an undo history records. Not posted when the value ends where it began | bool or double |
| `Cancelled` | Escape (the `ui_back` action's keys) during a change, or disabling it: the value at its start is back (a text box's text at entering) | double |
| `Pressed` | a press of any button reached the element | mouse data |
| `Released` | that press's release, or another's let go over it (`ReleaseOver`) | mouse data; `IsOverWidget` says whether it was over the element |
| `DoubleClicked` | after `Pressed`, at the second press in a row (`ClickCount` 2, not 3) | mouse data |
| `Scrolled` | a button with `TakesWheel` | mouse data with `ScrollDelta` |
| `ContextRequested` | a right press let go on the element, or the context menu action's key (`tbp:ui_context_menu`): then `Button` is the key, the place the element's middle, and `IsOverWidget` false | mouse data |
| `CellToggled` | a toggle cell of a list's or a tree's row was clicked | the row as a `UInt64` (a list's data index, a tree's node); the cell as the index |

The mouse data is in the event by value (`UIEvent.HasMouse`, `UIEvent.Mouse`: button, modifiers, screen position, scroll delta, whether over the element), so a handler needs no
widget. A compound's parts post their value events under the compound's id, and a row of fields' under the row's with the field's axis as the event's index
(`UIEvent.HasIndex`, `UIEvent.Index`, posted with `UIScreen_PostWidgetIndexedEvent`). A dialog's answer and a tool bar's item come the same way: the answer
or the item's id is the index. A list's and a tree's `Activated`, `DoubleClicked` and `ContextRequested` carry the row as a `UInt64` value (an id a double
cannot hold), and their selection's `Changed` and `Committed` the number selected; a colour picker's value events carry a `Color` or a `RenderColor`.

**Cues** (`UIEventKind_Cue`, [UIFramework.md](UIFramework.md), "Notifications"): elements post named sound cues for what they do, which nothing plays yet and which
cost nothing while nobody subscribes. The kinds post `Hover` (the pointer came onto it), `Press` (a press on it while enabled; `Refused` while disabled), `Release`,
`Activate` (a button's activation, a toggle's change) and `ValueStep` (a value's arrow step, a wheel notch, a reset, a paste, a typed value); a text box posts `Refused` for what its limits
or a masked box refuse and for an undo or redo with nothing left; the screen posts `Enter` and `Leave` for an edit element.
`UIElement_PostCue` and `UIElement_PostMouseCues` are the helpers a new kind uses. For a key's activation `Button` is the key. A disabled element posts none of these, and a toggle and a slider tell the input events as a button does.

A toggle's activation, a wheel notch and a reset are whole changes: `Changed` then `Committed` in one update. Code setting a value posts nothing, and
a value that is not a finite number is refused. A slider that loses its press (detached, hidden, input-disabled, or its press handed on) ends the drag there, with its commit if the
value moved. A key's change stops following when the slider stops taking input or showing: the value stays where it got to and the commit comes when the key comes up; off the screen
the slider hears nothing and gets no update, so that commit waits until it is back and sees the key up. A slider destroyed posts nothing more. After a cancel the key that made the
change does nothing until it is pressed anew.

## Themes

A theme is data, the `ui_theme` asset type (`ui_themes/`; the format is `references/asset_structure.md`, "UI themes"): a font and a
text size, a transition time, a hover delay, a palette, and per element kind its metrics, parts, custom values and look. Packs bring and
override themes by namespaced names. The whole UI changes at once, with no element's code changing.

- **Per screen:** `UIScreen_SetTheme` loads the theme under a new asset user of the screen's and retires the old one, which releases it and its textures.
  A font an element's text was laid out with stays held by that element's document until the element goes: as many as there are fonts, no more.
  A screen in the world can have a look of its own.
- **Never kept:** elements look values up at layout and at render (`UIElementLook_GetStyle`, then `UIThemeStyle_Get*`) and hold nothing into a
  theme past that, so a swap between an update and a render cannot leave a dangling pointer. Every lookup takes a NULL style and answers
  with the kind's default.
- **Variants merge when read:** a variant is a copy of its kind's values with what it names on top, down to one state's colour.
- **A palette:** a theme names its colours once (`"palette"`, at most `UI_THEME_MAX_PALETTE_COLORS`), and any other colour in it may be
  `"@name"`. A palette colour is a plain colour, never a reference, so references cannot loop. They are resolved when the file is read, so
  a loaded theme holds plain colours and code never sees a name; swapping one palette for another recolours everything that names it
  (`tbp:test_light` is `tbp:test_flat` with another palette).
- **A look by name:** a kind's block may name the look that draws its elements (`"look"`, `UITheme_GetLookName`). The theme carries only
  the name: the kit resolves it when an element is made ("An element: behaviour and a look"), so a theme names a look that a mod's code
  registers without the theme needing it to exist.
- **Custom values:** a look whose visuals the kind's parts do not describe reads values of its own from its style's `"custom"` block, by
  keys it chooses: `UIThemeStyle_FindCustomNumber`, `..Vector`, `..Color` (each with the default for a theme that leaves the key out) and
  `..Box` (NULL then). The standard button's `icon_spacing` is one. A variant starts with its kind's custom values and replaces or adds keys.
- **Hover text:** the `tooltip` kind is the box hover text shows in (its padding, its offset from the pointer, its wrap width, a background
  and a text colour), and `"tooltip_delay"` is how long the pointer rests before it shows (`UITheme_GetTooltipDelaySeconds`; a widget may
  have its own, `UITooltip_SetDelay`). The offset is where the box's top-left sits from the pointer, and may be negative: left of or above
  it. Kept on the surface, the box slides in past a side edge and goes to the other side of the pointer past the top or the bottom.
- **Bounds per part:** each metric part's row in `Kinds` (`source/UITheme.c`) carries its bounds (`UIThemePartDescriptor.Minimum`,
  `.Maximum`): sizes, paddings and spacings count from 0, an offset may point either way.
- **States:** normal, hovered, pressed, focused, disabled, selected, invalid, entered (an edit element being edited, so its keys are its
  own). A state a theme leaves out falls back to normal; entered falls back to focused first. `UIElementState_Choose` picks the one that
  shows: disabled, pressed, invalid, entered, selected, hovered, focused, normal. Both test themes give the slider's thumb an entered colour
  of its own, so a player can tell whether the arrows will move the slider or move on.
- **Fading:** `UIElementLook_Update` moves a weight per state over the transition time (in Update; Render only reads), so a state change
  that turns around half way never jumps.
- **What a theme survives:** a theme that does not load keeps what the screen had. A missing theme or part draws a plain box
  (`UIBoxStyle_DrawFallback`) and is logged once, on the update side (`UIScreen_ReportMissingThemePart`): never a crash, never a frame error.
  `"kind": "none"` is a part left empty on purpose. A texture that does not load, or whose art does not fit its margins, makes only its
  parts plain; a font that does not load leaves raylib's built-in one. Each is logged once when the theme loads.

### Box styles

`UIBoxStyle_Draw` draws through the `UIRenderContext`, so clips and tints apply. `flat` is a colour and optionally an outline, drawn
inside the box, at least one pixel. `nine_slice` is a texture, its margins in texels, a colour per state multiplied in and optionally
another source rectangle per state, a white nine-part button tinted per state. Corners keep their size times the pixel scale and the screen's
scale; sides stretch along their length and the centre both ways. `UINineSlice_GetQuads` shares every inner edge between neighbours, whole
pixels, so there are no gaps at any size. Margins larger than the box shrink evenly: both sides of an axis by one factor. Give nine-slice art
`"filtering": "point"` and `"wrap": "clamp"`.

### Text

`tbp:ui_small` is Noto Sans rasterized at 28 pixels, drawn at 13 to 16 UI pixels (bilinear, no mipmaps). Twice the drawn size on purpose: raylib
rounds a glyph's advance to whole pixels at the raster's size, so a font rasterized at 14 spaces its letters unevenly at every size and blurs when the
UI is scaled, and at 28 both go away while 14 pixels stays sharp enough to read (a 2:1 reduction with bilinear filtering is a box filter).

## The gallery

`sh compile/run_linux_x11.sh --ui-gallery` runs `UIGalleryFrame` instead of the world test, on seven pages (N switches), each in a scroll area, so the
whole page scrolls at small window sizes while its status line stays below it. It keeps its pixels
(`UIScalePolicy_ConstantPixels`): drag the window's edges, and the controls keep their size with more room around them. P switches to following the
window's height and back, to compare them. T switches the theme (`tbp:test_flat`, `tbp:test_nine_slice`, whose art `asset_source/scripts/placeholder_ui.py`
generates, and `tbp:test_light`, the first with another palette), + and - the player's scale, L the event log; Escape steps out one level at a time, and
quits once nothing is focused. Each event shows in the status line and in the log, with its button, modifiers and place (a drag's `Changed` events a few
times a second), and so does each element entered or left. Everything has hover text.

The first page: every element in every state it can be put in, in stacks that show content, fixed and filling sizes, over a plain background. A row tries the
routing: a button per click mode (left and middle activate), a stack that takes the wheel holding two buttons that leave it, a button made with `TakesWheel`,
a row with each pointer shape, and a box whose drag hides the cursor and reads the raw motion. Every slider has a default, so Backspace over one puts it back.
The keyboard box (the right panel) holds a button, two sliders and a toggle to try Tab, the arrows and Enter on: the box is a focusable panel holding
focusable elements, a group, so navigation reaches it as one, Enter goes in and Escape comes back out, and inside it Tab and the arrows stay among its
elements. There the arrows move past the sliders, Enter or a click enters one (the green outline, the thumb's entered colour), Tab carries the editing into the
next slider and only focuses the toggle.

The second page: the scale rows (the element design's table: fixed 100 x 24, fixed 400 x 24, fixed at scale 2 with its text clipped, by content at scale 2,
fixed 100 x 24 in a box of scale 2, so 200 x 48), a panel that turns once in 20 seconds with a button and a slider that work where they show, a panel that
slides in by its visual offset (its button slides it in again), a second look of the button that writes only Render and Measure (a diamond in the theme's
colours for the button's state, with no fade and no text), and buttons with placeholder letter icons.

The third page: a menu bar (File, Edit, View, Help: icons, shortcut texts, a submenu, separators, disabled and checked items; the View menu's items
change the theme, the scale and the page, after the screen's update), a row under it with a dropdown near each top corner, a box with a context menu
(submenus three deep, a Locked item whose check remembers, a disabled item; a button in it with no menu of its own, so its request walks up to the box's,
and an inner box with a menu of its own), a button opening a modal dialog (a plain panel with Yes and No, centred over a backdrop: Enter says yes, Escape
no), dropdowns of three options and of a thousand from a provider, showing four rows, always opening up, mixed and disabled, two scroll areas (down only
with forty buttons to Tab through; both ways with its bars always shown), and a long end with a button at the bottom for Tab to scroll to. A status bar
closes the page: its message is the status line, for five seconds, then the hover text where the pointer rests; a dropdown stands at each of its ends,
near the window's bottom corners.

The fourth page: text boxes. On one line: with a placeholder, with a default (Backspace over it, not entered, puts it back), masked, filtered to six digits,
limited to twelve characters, disabled, marked invalid by code, and a line of code coloured by style ranges (the colours follow the theme's text, light
or dark). Of several lines: wrapped, not wrapped (Tab types a tab, it scrolls both ways) and read-only. A form of three boxes to Tab through. Every edit
shows in the status line with the text (a masked box's only as its length).

The fifth page: values. Sliders stepped, log, power, of whole numbers and over a value list (each value the same length of track), and three upright ones;
property boxes with no limits and a unit, in a range with a fill, with a soft range, log with its fill by the conversion, of whole numbers, mixed, disabled,
of a float's value and of a string; compound sliders across (named, and with no name) and upright; a form of labelled rows (a dropdown, a toggle, a text box,
a property box, and a stacked one); rows of fields (a position, a rotation, a size of two, four for a quaternion, and one whose Y is mixed), whose events
show their axis.

The sixth page: what an editor's windows are built from. A tool bar across the page (buttons, two toggles, four choices of one group, a button with a text and
a disabled one) and an upright one beside the tree; a list of a hundred thousand rows with two toggle cells each (shown, locked: the page's own data,
flipped by `CellToggled`), every seventh row dimmed; a tree twelve levels deep whose nodes are worked out from their ids, its expanded state the page's,
whose nodes drag but no list takes them (the refused look); two lists, the desk and the tray, that drag things between them and within one (the moved
rows land selected where the line was); colour pickers with alpha, opaque, a light's `RenderColor`, mixed and disabled; and three dialogs: unsaved changes
(Save the default, Cancel the cancel), a rename with its field entered and the old name selected, and a long message that wraps. The status line tells each
selection, opened row, toggled cell, colour, answer and drop.

The seventh page: windows, of four types the gallery registers with the program's window types while it runs (`source/UIGalleryWindows.c`): Fitting
(both ways fit: a form of labelled rows it gets no smaller than), Scrolling (across fits, down scrolls: thirty buttons and the bar), Fixed (no edges,
no maximize, all of it at a scale of 1.25, as a screen in the world is) and Notes (a text box filling it, several at once, whose close asks with a
dialog while it says something). They open at their places the first time the page shows and hide while another page shows; the page's menu bar has
a Window menu that lists every type and opens it (a single one comes to the front, restored). While the page shows, the screen's workspace
runs from under the menu bar to the status line, so a maximized window leaves both.

## Rules for a new element

- Behaviour and looks apart: the capability holds logical state and content, never a look, a colour or laid-out text; the look draws it. Calling code
  uses the capability by id.
- Input only through the hooks; `UIScreen_GetInput` for an action's keys (`UIElement_IsActionKey`, `UIElement_IsActionPress` with its modifiers counted) and held state. A key a player could rebind is
  an action's.
- What opens above everything opens through the popup layer ([UIFramework.md](UIFramework.md), "Popups"), owned by the element that opened it; a list in it that
  takes the arrows is an edit element focused and entered as it opens (`UIPopupOptions.FocusId`), so its keys work at once.
- Content that can be larger than the box holds the scrollable piece and lets a scroll bar move it; rows of data go through the row recycler.
- An element that uses the arrows or typing is an **edit element**: its descriptor sets `NeedsEntering`, so those keys reach it only once it is entered, and the screen leaves it on Escape,
  Enter, Tab and focus moving ([UIFramework.md](UIFramework.md), "Entering elements"). It answers `OnEnter` and `OnLeave` where entering and a cancel mean something to it, uses Escape only
  for what it can take back on its own (a change in progress), and passes `Widget_IsEntered` to `UIElementState_Choose`. One whose press does something else first (a scrub) sets `EntersItself`
  and enters itself.
- A press that starts something answers `OnPressLost` as well as the release, and what stops the wheel says so in `OnScrollInput`. `UIElement_PostMouseEvents` tells the input events,
  `UIElement_OnContextKey` the menu key.
- Render reads state, never writes it: state changes in the hooks and in Update. Report a missing part from Update, not Render.
- Draw in whole surface pixels: `UIRenderContext_GetPixelBox`, then the `UIRenderContext_RenderPixel*` calls. A render transform turns them as they are.
- Answer `Measure` in your own UI pixels, and call `Widget_InvalidateMeasure` from whatever changes the answer. Answer without changing what you draw: a width you
  are asked about may not be the width you end up with.
- Turn UI pixels into surface pixels with the widget's effective scale (`Widget_GetEffectiveScale`), never the screen's: text, corners, padding, outlines.
- Post the kind's cues (`UIElement_PostCue`) for what it does, as the standard kinds do.
- An element that holds a number holds the value piece (`UIValue.h`) in its capability and declares it (`UIElementCapability_Value`), so `UIValue_*` reach it,
  and posts through its change helpers; it gives the piece hooks (`UIValueHooks`) for what code changes, and a part of a compound posts for the compound
  (`UIValueCapability_SetEventTarget`).
- Draw nothing in an empty box (`UIElement_IsBoxEmpty`): an element made in a handler has no box until its first layout, and a stack may leave it no room.
- Work on any surface: read the pointer, the keyboard and sizes only from what the screen gives. A world screen's pixels are its render target's.
