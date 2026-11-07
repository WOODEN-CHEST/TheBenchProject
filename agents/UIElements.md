# UI elements: the kit, themes and looks

The elements calling code builds a UI from: panels, labels, buttons, toggles, sliders and stacks, sized in UI pixels and
drawn through a theme. They stand on the framework in [UIFramework.md](UIFramework.md) (screens, widgets, input routing,
notifications).

| Module | Files | What it is |
|---|---|---|
| UIPlacement | `include/UIPlacement.h`, `source/UIPlacement.c` | Pure: placements (anchors and UI pixel offsets), insets, a stack's hints, turning a placement into a widget's normalized box. |
| UIBoxStyle | `include/UIBoxStyle.h`, `source/UIBoxStyle.c` | How a box is drawn: nothing, flat or nine-slice, a colour per state. The nine quads and resolving a state are pure. |
| UITheme | `include/UITheme.h`, `source/UITheme.c`, `source/UIThemeDefinition.c`, private `source/UIThemeInternal.h` | The `ui_theme` asset type: the look of every element kind as data. |
| UIElement | `include/UIElement.h`, `source/UIElement.c` | What every element kind shares: its look, its text, its press behaviour, its options and its events. |
| UIKit | `include/UIKit.h`, `source/UIKit.c` | The one way to make elements, over one widget factory: capabilities, standard types, the table from kind to type. |
| StackWidget | `include/StackWidget.h`, `source/StackWidget.c` | Children in a row or a column. Its arithmetic (`UIStackLayout`) is shared with the panel. |
| UIPanel | `include/UIPanel.h`, `source/UIPanel.c` | A background and padding for what it holds. |
| UILabel | `include/UILabel.h`, `source/UILabel.c`, with `LabelWidget` | `LabelWidget` as an element of the kit. |
| UIButton, UIToggle, UISlider | `include/UI<Kind>.h`, `source/UI<Kind>.c` | The element kinds with input. |
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

Teardown: the screen (it destroys the elements), the kit, then the factories. Every creation is whole or nothing: on a failure
no widget stays behind and the id is `WIDGET_ID_INVALID`. Widgets added during a tick are placed from the next tick on, like
every widget attached during a tick.

## An element: behaviour, capability, look

An element kind has a **capability struct** holding its state and its contract (`UIButtonCapability`: its text, its enabled
flag, its press), **behaviour helpers** on that struct which interpret input and post events (`UIButtonCapability_OnMouseInput`),
and one **standard widget type** that embeds the capability, calls the helpers from its hooks and draws through the theme.
Calling code names the capability, by widget id (`UIButton_SetText(screen, id, text)`), never a widget type or a look.

A variant costs, from cheapest:
1. **Theme data:** colours, sizes, nine-slice art. Almost everything.
2. **A named variant in the theme** (`UIElementOptions.Variant`, `UIElement_SetVariant`).
3. **A widget type of its own** that embeds the same capability, calls the same helpers and draws differently or holds other
   widgets. `UIWidgetFactory_RegisterType` with `UIKit_GetCapabilityId`, then `UIKit_SetElementType`: from then on `UIKit_Create*` makes it,
   and calling code does not change. `tests/UIElementTests.c` has one (`OwnButtonWidget`).

**Finding the kit.** `UIKit_FromScreen` goes through the screen's factory, which is how the by-id functions find their capability
ids. A second kit over a factory is refused.

### Adding an element kind
1. A value in `UIElementKind`, a part enum and a row in `Kinds` (`source/UITheme.c`): the theme's reader and its checks come from
   that table, up to `UI_THEME_MAX_PARTS` values per kind.
2. If it has input of its own, a value in `UIElementCapability`, and a capability struct with its helpers, in a module of its own.
3. The kit's creation function, and its row in `RegisterEverything` and `KindCapabilities` (`source/UIKit.c`).
4. Its section in `references/asset_structure.md`, "UI themes", and the gallery.

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
| `Changed` | the value moved and may go on: during a drag, at each key step | bool or double |
| `Committed` | the change is done: the release, the key's release, the press lost; what an undo history records. Not posted when the value ends where it began | bool or double |
| `Cancelled` | Escape (the `ui_back` action's keys) during a change, or disabling it: the value at its start is back | double |
| `Pressed` | a press of any button reached the element | mouse data |
| `Released` | that press's release, or another's let go over it (`ReleaseOver`) | mouse data; `IsOverWidget` says whether it was over the element |
| `DoubleClicked` | after `Pressed`, at the second press in a row (`ClickCount` 2, not 3) | mouse data |
| `Scrolled` | a button with `TakesWheel` | mouse data with `ScrollDelta` |
| `ContextRequested` | a right press let go on the element, or the context menu action's key (`tbp:ui_context_menu`): then `Button` is the key, the place the element's middle, and `IsOverWidget` false | mouse data |

The mouse data is in the event by value (`UIEvent.HasMouse`, `UIEvent.Mouse`: button, modifiers, screen position, scroll delta, whether over the element), so a handler needs no
widget. For a key's activation `Button` is the key. A disabled element posts none of these, and a toggle and a slider tell the input events as a button does.

A toggle's activation, a wheel notch and a reset are whole changes: `Changed` then `Committed` in one update. Code setting a value posts nothing, and
a value that is not a finite number is refused. A slider that loses its press (detached, hidden, input-disabled, or its press handed on) ends the drag there, with its commit if the
value moved. A key's change stops following when the slider stops taking input or showing: the value stays where it got to and the commit comes when the key comes up; off the screen
the slider hears nothing and gets no update, so that commit waits until it is back and sees the key up. A slider destroyed posts nothing more. After a cancel the key that made the
change does nothing until it is pressed anew.

## Themes

A theme is data, the `ui_theme` asset type (`ui_themes/`; the format is `references/asset_structure.md`, "UI themes"): a font and a
text size, a transition time, and per element kind its metrics and parts. Packs bring and override themes by namespaced names. The whole
UI changes at once, with no element's code changing.

- **Per screen:** `UIScreen_SetTheme` loads the theme under a new asset user of the screen's and retires the old one, which releases it and its textures.
  A font an element's text was laid out with stays held by that element's document until the element goes: as many as there are fonts, no more.
  A screen in the world can have a look of its own.
- **Never kept:** elements look values up at layout and at render (`UIElementLook_GetStyle`, then `UIThemeStyle_Get*`) and hold nothing into a
  theme past that, so a swap between an update and a render cannot leave a dangling pointer. Every lookup takes a NULL style and answers
  with the kind's default.
- **Variants merge when read:** a variant is a copy of its kind's values with what it names on top, down to one state's colour.
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

`sh compile/run_linux_x11.sh --ui-gallery` runs `UIGalleryFrame` instead of the world test: every element in every state it can be put in,
in stacks that show content, fixed and filling sizes, over a plain background. It keeps its pixels (`UIScalePolicy_ConstantPixels`): drag the window's edges, and the controls keep their size with more room around them. P switches to following the
window's height and back, to compare them. T switches the theme (`tbp:test_flat`, `tbp:test_nine_slice`, whose art
`asset_source/scripts/placeholder_ui.py` generates), + and - the player's scale, L the event log; Escape steps out one level at a time, and quits once nothing is focused. Each event shows in its status line and
in the log, with its button, modifiers and place (a drag's `Changed` events a few times a second), and so does each element entered or left. A row tries the routing: a button per click mode (left and middle activate), a stack
that takes the wheel holding two buttons that leave it, a button made with `TakesWheel`, a row with each pointer shape, and a box whose drag hides the cursor and reads the raw motion. Every
slider has a default, so Backspace over one puts it back. The keyboard box (the right panel) holds a button, two sliders and a toggle to try Tab, the arrows and Enter on: the box is a
focusable panel holding focusable elements, a group, so navigation reaches it as one, Enter goes in and Escape comes back out, and inside it Tab and the arrows stay among its elements.
There the arrows move past the sliders, Enter or a click enters one (the green outline, the thumb's entered colour), Tab carries the editing into the next slider and only focuses the toggle.
Escape steps out one level at a time, and quits once nothing is focused.

## Rules for a new element

- Input only through the hooks; `UIScreen_GetInput` for an action's keys (`UIElement_IsActionKey`, `UIElement_IsActionPress` with its modifiers counted) and held state. A key a player could rebind is
  an action's.
- An element that uses the arrows or typing is an **edit element**: its descriptor sets `NeedsEntering`, so those keys reach it only once it is entered, and the screen leaves it on Escape,
  Enter, Tab and focus moving ([UIFramework.md](UIFramework.md), "Entering elements"). It answers `OnEnter` and `OnLeave` where entering and a cancel mean something to it, uses Escape only
  for what it can take back on its own (a change in progress), and passes `Widget_IsEntered` to `UIElementState_Choose`. One whose press does something else first (a scrub) sets `EntersItself`
  and enters itself.
- A press that starts something answers `OnPressLost` as well as the release, and what stops the wheel says so in `OnScrollInput`. `UIElement_PostMouseEvents` tells the input events,
  `UIElement_OnContextKey` the menu key.
- Render reads state, never writes it: state changes in the hooks and in Update. Report a missing part from Update, not Render.
- Draw in whole surface pixels: `UIRenderContext_GetPixelBox`, then the `UIRenderContext_RenderPixel*` calls.
- Answer `Measure` in UI pixels, and call `Widget_InvalidateMeasure` from whatever changes the answer. Answer without changing what you draw: a width you are
  asked about may not be the width you end up with.
- Draw nothing in an empty box (`UIElement_IsBoxEmpty`): an element made in a handler has no box until its first layout, and a stack may leave it no room.
- Work on any surface: read the pointer, the keyboard and sizes only from what the screen gives. A world screen's pixels are its render target's.
