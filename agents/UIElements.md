# UI elements: the kit, themes and looks

The elements calling code builds a UI from: panels, labels, buttons, toggles, sliders, stacks, scroll areas and scroll bars,
dropdowns, menus, menu bars, status bars and text boxes, sized in UI pixels and drawn through a theme. They stand on the framework in [UIFramework.md](UIFramework.md) (screens, widgets, input routing,
notifications).

| Module | Files | What it is |
|---|---|---|
| UIPlacement | `include/UIPlacement.h`, `source/UIPlacement.c` | Pure: placements (anchors and UI pixel offsets), insets, a stack's hints, turning a placement into a widget's normalized box. |
| UIBoxStyle | `include/UIBoxStyle.h`, `source/UIBoxStyle.c` | How a box is drawn: nothing, flat or nine-slice, a colour per state. The nine quads and resolving a state are pure. |
| UITheme | `include/UITheme.h`, `source/UITheme.c`, `source/UIThemeDefinition.c`, private `source/UIThemeInternal.h` | The `ui_theme` asset type: the look of every element kind as data. |
| UIElement | `include/UIElement.h`, `source/UIElement.c` | What every element kind shares: its options, its events and cues, plain text content, icons, the press behaviour, and the helpers looks draw with. |
| UIKit | `include/UIKit.h`, `source/UIKit.c`, private `source/UIKitInternal.h` | The one way to make elements, over one widget factory: capabilities, the looks registered by kind and name, the widget types it composes. |
| UIStandardLooks | `include/UIStandardLooks.h`, `source/UIStandardLooks.c` | The kit's standard look (`tbp:test_box`) for the panel, the button, the toggle, the slider, the scroll bar, the scroll area, the dropdown's head, the menu bar, the status bar and the text box; the widget that shows hover text; a modal popup's backdrop. |
| StackWidget | `include/StackWidget.h`, `source/StackWidget.c` | Children in a row or a column. Its arithmetic (`UIStackLayout`) is shared with the panel. |
| UIPanel | `include/UIPanel.h`, `source/UIPanel.c` | A background and padding for what it holds. |
| UILabel | `include/UILabel.h`, `source/UILabel.c`, with `LabelWidget` | `LabelWidget` as an element of the kit. |
| UIButton, UIToggle, UISlider | `include/UI<Kind>.h`, `source/UI<Kind>.c` | The element kinds with input. |
| UIScrollable | `include/UIScrollable.h`, `source/UIScrollable.c` | The scrollable piece: content and view sizes, the clamped offset, a mode per axis; its arithmetic pure. |
| UIScrollbar, UIScrollArea | `include/UI<Kind>.h`, `source/UI<Kind>.c` | A bar bound to another element's scrollable piece; content larger than its box, with two bars. |
| UIChoice | `include/UIChoice.h`, `source/UIChoice.c` | The choice piece: options as texts or from providers, the selection, mixed. |
| UIRowRecycler | `include/UIRowRecycler.h`, `source/UIRowRecycler.c` | Widgets for the rows in view only, reused as the view scrolls. |
| UIDropdown | `include/UIDropdown.h`, `source/UIDropdown.c` | One choice of several: a head and a list in a popup. |
| UIMenu | `include/UIMenu.h`, `source/UIMenu.c`, private `source/UIMenuInternal.h` | Menus as data, each open level a popup list of items. |
| UIMenuBar, UIStatusBar | `include/UI<Kind>.h`, `source/UI<Kind>.c` | A strip of entries opening menus; a strip of sections and a line of text. |
| UITextEdit | `include/UITextEdit.h`, `source/UITextEdit.c` | The text piece, pure: a UTF-8 text with its limits, a caret and a selection, words, edits with the incoming text's rules, and a grouped undo with a budget. |
| UITextBox | `include/UITextBox.h`, `source/UITextBox.c`, `source/UITextBoxInput.c`, private `source/UITextBoxInternal.h` | Text to type in or to read and select, on one line or several: the keys, the mouse, the clipboard, undo, scrolling, a placeholder, masked and read-only boxes, style ranges. |
| UIGalleryFrame | `include/UIGalleryFrame.h`, `source/UIGalleryFrame.c` | Every element in every state: `--ui-gallery`. |

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
`UISliderLookBase`, `UIScrollbarLookBase`, `UIScrollAreaLookBase`, `UIDropdownLookBase`, `UIMenuBarLookBase`, `UIStatusBarLookBase`, `UITextBoxLookBase`: the
`Widget`, then the capability) and a `UILookVTable`:

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
(its list and items), a dropdown's open list and rows, a menu bar's entries and the backdrop are not replaceable yet (`// TODO:` in `UIKit.c`'s
kinds): `UIKit_RegisterLook` refuses the label, the tooltip, the menu and the backdrop kinds, and the other parts are the kit's own widget types.

A slider look also tells the behaviour how long its thumb is (`UISliderCapability_SetThumbLength`, in its own UI pixels, from its Update): a press within
the thumb holds it where it was grabbed, and the thumb's centre travels the track less its length. A look that never says has a thumb of no length: the
pointer is the value.

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

**Slider** (`UISlider.h`). A value in a range, as a double, with an optional step. It is an **edit element** ([UIFramework.md](UIFramework.md),
"Entering elements"): moving the focus onto it changes nothing, and Tab and the arrows go past it until it is entered, by Enter, any press on it
or code. A left press on the track brings the thumb under the pointer; a press on the thumb holds it where it was grabbed, so a click on it
changes nothing. Dragging follows the pointer wherever it goes, past the box and past the window: past an end the value is that end. Entered,
all four arrows are its own: `ui_up` and `ui_right` step higher, `ui_down` and `ui_left` lower (repeats follow; at an end they do nothing and
are still consumed), Home and End jump. Each press is a whole change, committed when its key comes up. Escape during a held key or a drag
cancels that change and the slider stays entered; otherwise it leaves Escape to the screen, which leaves it, and the presses already committed
stay. Left any way (Enter, Tab, focus moving, code), it ends a drag or a held key's change there, with its commit (a cancel by code takes it
back), and what is left of that drag or key does nothing. The wheel is the slider's only while it is entered or with Ctrl: otherwise it goes on up the tree, so scrolling a panel never changes a
value by accident. With `HasDefault` and `Default` (`UISlider_SetDefault`), the reset action (`tbp:ui_reset`, Backspace), focused or only hovered,
entered or not, puts it back, a whole change; during a change it is used and does nothing. A range that is no whole number of steps keeps its
upper end reachable: the end counts as a step.

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

**Context menus** are a service of the screen ([UIFramework.md](UIFramework.md), "Context menus"): a provider set on a widget answers a right click or
the menu key on it or on anything inside it that has no provider of its own, and opens a menu with `UIMenu_OpenAt`.

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
| `Changed` | the value moved and may go on: during a drag, at each key step; a dropdown's choice (with `Committed`); a text box's edit | bool or double (a dropdown's index, a text box's length in bytes) |
| `Committed` | the change is done: the release, the key's release, the press lost, a text box left after a visit that changed it; what an undo history records. Not posted when the value ends where it began | bool or double |
| `Cancelled` | Escape (the `ui_back` action's keys) during a change, or disabling it: the value at its start is back (a text box's text at entering) | double |
| `Pressed` | a press of any button reached the element | mouse data |
| `Released` | that press's release, or another's let go over it (`ReleaseOver`) | mouse data; `IsOverWidget` says whether it was over the element |
| `DoubleClicked` | after `Pressed`, at the second press in a row (`ClickCount` 2, not 3) | mouse data |
| `Scrolled` | a button with `TakesWheel` | mouse data with `ScrollDelta` |
| `ContextRequested` | a right press let go on the element, or the context menu action's key (`tbp:ui_context_menu`): then `Button` is the key, the place the element's middle, and `IsOverWidget` false | mouse data |

The mouse data is in the event by value (`UIEvent.HasMouse`, `UIEvent.Mouse`: button, modifiers, screen position, scroll delta, whether over the element), so a handler needs no
widget.

**Cues** (`UIEventKind_Cue`, [UIFramework.md](UIFramework.md), "Notifications"): elements post named sound cues for what they do, which nothing plays yet and which
cost nothing while nobody subscribes. The kinds post `Hover` (the pointer came onto it), `Press` (a press on it while enabled; `Refused` while disabled), `Release`,
`Activate` (a button's activation, a toggle's change) and `ValueStep` (a slider's arrow step, wheel notch or reset); a text box posts `Refused` for what its limits
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

`sh compile/run_linux_x11.sh --ui-gallery` runs `UIGalleryFrame` instead of the world test, on four pages (N switches), each in a scroll area, so the
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
- Draw nothing in an empty box (`UIElement_IsBoxEmpty`): an element made in a handler has no box until its first layout, and a stack may leave it no room.
- Work on any surface: read the pointer, the keyboard and sizes only from what the screen gives. A world screen's pixels are its render target's.
