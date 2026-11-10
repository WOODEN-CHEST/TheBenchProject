#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// Vector2 and Color are part of the lookups' answers; the box styles are what a theme holds.
#include "raylib/raylib.h"
#include "UIBoxStyle.h"


/**
 * @file UITheme.h
 * @brief The look of the UI's elements as data: the ui_theme asset type (references/asset_structure.md, "UI themes").
 *
 * A theme names a font and a text size, a transition time for state changes, how long the pointer rests before hover text
 * shows, and per element kind its metrics (padding, a minimum size, a thumb's size) and its parts (a slider's track, fill and
 * thumb), each part a box style or a colour per state. Named variants (a "danger" button) change only what they name. The
 * whole UI changes at once with UIScreen_SetTheme, with no element's code changing: art arrives as a file.
 *
 * A theme's palette names its colours once ("accent": "#9ec1ff"); any colour of the theme may then be written "@accent".
 * References are resolved when the file is read, so a lookup answers a plain colour and swapping one palette for another
 * changes everything that names it. A kind's block may also hold custom values (numbers, vectors, colours and boxes by
 * key) for a look with visuals the kind's parts do not describe, and may name that look ("look": "tbp:round_button"):
 * looks are registered in code, and the theme only carries the name.
 *
 * A definition refuses unknown keys and checks every rule when it is read. The textures and the font it names are the
 * theme's dependencies: they stay loaded as long as it does. A texture that does not load, or whose art does not fit its
 * margins, leaves the boxes using it (parts or custom boxes) drawing plain and is logged once; the rest of the theme works.
 *
 * Elements look values up at layout and at render, through UITheme_FindStyle and the UIThemeStyle_Get* and
 * UIThemeStyle_FindCustom* functions, and never keep what they got past that: the screen's theme can change at any time.
 * Every lookup takes a NULL style, so an element on a screen without a theme simply gets the defaults.
 */


// Macros.
/** @brief The most named variants of one element kind in a theme. */
#define UI_THEME_MAX_VARIANTS ((size_t)16)
/** @brief The longest variant name, in bytes, its NUL included. */
#define UI_THEME_VARIANT_NAME_CAPACITY ((size_t)32)
/** @brief The most different textures one theme may name. */
#define UI_THEME_MAX_TEXTURES ((size_t)64)
/** @brief The most parts and metrics one element kind may have. */
#define UI_THEME_MAX_PARTS ((size_t)16)
/** @brief The largest metric a theme may give, in UI pixels. */
#define UI_THEME_MAX_METRIC 4096.0f
/** @brief The bounds of a theme's text size, in UI pixels, and its default. */
#define UI_THEME_MIN_TEXT_SIZE 4.0f
#define UI_THEME_MAX_TEXT_SIZE 256.0f
#define UI_THEME_DEFAULT_TEXT_SIZE 14.0f
/** @brief The longest transition a theme may give, in seconds. */
#define UI_THEME_MAX_TRANSITION_SECONDS 5.0f
/** @brief The bounds of a nine-slice's pixel scale. */
#define UI_THEME_MIN_PIXEL_SCALE 0.125f
#define UI_THEME_MAX_PIXEL_SCALE 16.0f
/** @brief The largest outline, in UI pixels. */
#define UI_THEME_MAX_OUTLINE 64.0f
/** @brief The most colours a theme's palette may name. */
#define UI_THEME_MAX_PALETTE_COLORS ((size_t)64)
/** @brief The longest palette colour name, in bytes, its NUL included. */
#define UI_THEME_PALETTE_NAME_CAPACITY ((size_t)32)
/** @brief The most custom values one style may hold; a variant counts the ones it inherits from its kind. */
#define UI_THEME_MAX_CUSTOM_VALUES ((size_t)16)
/** @brief The longest custom value key, in bytes, its NUL included. */
#define UI_THEME_CUSTOM_KEY_CAPACITY ((size_t)32)
/** @brief The largest magnitude of a custom number and of each component of a custom vector. */
#define UI_THEME_MAX_CUSTOM_NUMBER 1000000.0f
/** @brief The longest the pointer may have to rest before hover text shows, in seconds. */
#define UI_THEME_MAX_TOOLTIP_DELAY_SECONDS 10.0f
/** @brief How long the pointer rests before hover text shows, in seconds, when a theme leaves it out. */
#define UI_THEME_DEFAULT_TOOLTIP_DELAY_SECONDS 0.5f


// Types.
/** @brief A theme asset; loaded with AssetManager_LoadUITheme, read through the functions below. */
typedef struct UIThemeStruct UITheme;
/** @brief The values of one element kind or of one of its variants; borrowed from a theme. */
typedef struct UIThemeStyleStruct UIThemeStyle;

/**
 * @brief The kinds of element a theme describes, one block each under its "elements".
 */
typedef enum UIElementKindEnum
{
    /** @brief A background and padding for what it holds. */
    UIElementKind_Panel,
    /** @brief Text. */
    UIElementKind_Label,
    /** @brief A button. */
    UIElementKind_Button,
    /** @brief A checkbox: on, off or mixed. */
    UIElementKind_Toggle,
    /** @brief A value in a range. */
    UIElementKind_Slider,
    /** @brief Hover text: a box of text near the pointer. */
    UIElementKind_Tooltip,
    /** @brief A scroll bar: a track and a thumb. */
    UIElementKind_Scrollbar,
    /** @brief Content larger than its box, clipped, with bars. */
    UIElementKind_ScrollArea,
    /** @brief One choice of several: a head showing it and a list that opens. */
    UIElementKind_Dropdown,
    /** @brief A popup list of items: a context menu, a menu bar's menu, a submenu. */
    UIElementKind_Menu,
    /** @brief A strip of menu entries. */
    UIElementKind_MenuBar,
    /** @brief A strip with left, centre and right sections and a message. */
    UIElementKind_StatusBar,
    /** @brief What lies behind a modal popup: it takes every press. */
    UIElementKind_Backdrop,
    /** @brief Text to type in, or to read and select: one line or several, writable or read-only. */
    UIElementKind_TextBox,
    /** @brief A value in a box: typed in, or dragged sideways if it is a number. */
    UIElementKind_PropertyBox,
    /** @brief A label and any element beside it or above it. */
    UIElementKind_Labelled,
    /** @brief A column whose labelled rows share one label width. */
    UIElementKind_Form,
    /** @brief A slider, a property box and a name, one value. */
    UIElementKind_CompoundSlider,
    /** @brief Two to four property boxes in a row, each with an axis tag. */
    UIElementKind_VectorRow,
    /** @brief Rows of data, only those in view made: a selection, a current row, icons and toggle cells. */
    UIElementKind_List,
    /** @brief A list whose rows nest: a row per shown node, indented, with an expander. */
    UIElementKind_Tree,
    /** @brief A modal dialog: a title, a message, content and a row of buttons. */
    UIElementKind_Dialog,
    /** @brief A colour picker: a swatch, and a popup with a square, strips and fields. */
    UIElementKind_ColorPicker,
    /** @brief A strip of icon buttons and separators. */
    UIElementKind_Toolbar,
    /** @brief What a drag shows: what is dragged beside the pointer, and a frame over the target that would take it. */
    UIElementKind_Drag,
    /** @brief A window: a frame with a title bar, its caption buttons, edges to resize it and content. */
    UIElementKind_Window,
    /** @brief A workspace: its docking areas' tab strips and tabs, the splitters between them, the dock's background, and the zones a
     *  window dragged over the dock shows. */
    UIElementKind_Workspace,
    /** @brief A render view: a picture its host draws (a world, a preview), its background where there is none, and a frame over its
     *  edge. */
    UIElementKind_RenderView,
    /** @brief The number of kinds. */
    UIElementKind_Count
} UIElementKind;

/** @brief A panel's values in a theme. */
typedef enum UIPanelPartEnum
{
    /** @brief "padding": horizontal and vertical, UI pixels. */
    UIPanelPart_Padding,
    /** @brief "spacing": between its children, UI pixels. */
    UIPanelPart_Spacing,
    /** @brief "min_size": UI pixels. */
    UIPanelPart_MinSize,
    /** @brief "background": a box. */
    UIPanelPart_Background,
    /** @brief The number of a panel's values. */
    UIPanelPart_Count
} UIPanelPart;

/** @brief A label's values in a theme. */
typedef enum UILabelPartEnum
{
    /** @brief "text": a colour per state. */
    UILabelPart_Text,
    /** @brief The number of a label's values. */
    UILabelPart_Count
} UILabelPart;

/** @brief A button's values in a theme. */
typedef enum UIButtonPartEnum
{
    /** @brief "padding": horizontal and vertical around its text, UI pixels. */
    UIButtonPart_Padding,
    /** @brief "min_size": UI pixels. */
    UIButtonPart_MinSize,
    /** @brief "background": a box. */
    UIButtonPart_Background,
    /** @brief "text": a colour per state. */
    UIButtonPart_Text,
    /** @brief The number of a button's values. */
    UIButtonPart_Count
} UIButtonPart;

/** @brief A toggle's values in a theme. */
typedef enum UITogglePartEnum
{
    /** @brief "box_size": the checkbox's size, UI pixels. */
    UITogglePart_BoxSize,
    /** @brief "spacing": between the box and the text, UI pixels. */
    UITogglePart_Spacing,
    /** @brief "min_size": UI pixels. */
    UITogglePart_MinSize,
    /** @brief "box": a box. */
    UITogglePart_Box,
    /** @brief "check": a box, drawn inside it while on, as a bar while mixed. */
    UITogglePart_Check,
    /** @brief "text": a colour per state. */
    UITogglePart_Text,
    /** @brief The number of a toggle's values. */
    UITogglePart_Count
} UITogglePart;

/** @brief A slider's values in a theme. */
typedef enum UISliderPartEnum
{
    /** @brief "min_size": UI pixels. */
    UISliderPart_MinSize,
    /** @brief "thumb_size": UI pixels. */
    UISliderPart_ThumbSize,
    /** @brief "track_thickness": UI pixels. */
    UISliderPart_TrackThickness,
    /** @brief "track": a box. */
    UISliderPart_Track,
    /** @brief "fill": a box, the track from its start to the thumb. */
    UISliderPart_Fill,
    /** @brief "thumb": a box. */
    UISliderPart_Thumb,
    /** @brief The number of a slider's values. */
    UISliderPart_Count
} UISliderPart;

/** @brief Hover text's values in a theme. */
typedef enum UITooltipPartEnum
{
    /** @brief "padding": horizontal and vertical around its text, UI pixels. */
    UITooltipPart_Padding,
    /** @brief "offset": where its top-left sits from the pointer, UI pixels; negative components put it left of or above the
     *  pointer. */
    UITooltipPart_Offset,
    /** @brief "max_width": UI pixels; its text wraps past it. */
    UITooltipPart_MaxWidth,
    /** @brief "background": a box. */
    UITooltipPart_Background,
    /** @brief "text": a colour per state. */
    UITooltipPart_Text,
    /** @brief The number of hover text's values. */
    UITooltipPart_Count
} UITooltipPart;

/** @brief A scroll bar's values in a theme. */
typedef enum UIScrollbarPartEnum
{
    /** @brief "thickness": how wide the bar is across, UI pixels. */
    UIScrollbarPart_Thickness,
    /** @brief "min_thumb_length": the shortest the thumb gets along the track, UI pixels. */
    UIScrollbarPart_MinThumbLength,
    /** @brief "thumb_inset": the gap between the thumb and the track's sides across, UI pixels. */
    UIScrollbarPart_ThumbInset,
    /** @brief "track": a box, the whole bar. */
    UIScrollbarPart_Track,
    /** @brief "thumb": a box. */
    UIScrollbarPart_Thumb,
    /** @brief The number of a scroll bar's values. */
    UIScrollbarPart_Count
} UIScrollbarPart;

/** @brief A scroll area's values in a theme. */
typedef enum UIScrollAreaPartEnum
{
    /** @brief "background": a box behind the content. */
    UIScrollAreaPart_Background,
    /** @brief The number of a scroll area's values. */
    UIScrollAreaPart_Count
} UIScrollAreaPart;

/** @brief A dropdown's values in a theme: its head, and the list it opens. */
typedef enum UIDropdownPartEnum
{
    /** @brief "padding": horizontal and vertical around the head's text, UI pixels; the rows' text is inset by its horizontal one. */
    UIDropdownPart_Padding,
    /** @brief "min_size": the head's, UI pixels. */
    UIDropdownPart_MinSize,
    /** @brief "arrow_size": the width of the head's arrow and of the list's scroll arrows, UI pixels. */
    UIDropdownPart_ArrowSize,
    /** @brief "background": a box, the head. */
    UIDropdownPart_Background,
    /** @brief "text": a colour per state, the head's text. */
    UIDropdownPart_Text,
    /** @brief "arrow": a colour per state, the head's arrow and the list's scroll arrows. */
    UIDropdownPart_Arrow,
    /** @brief "list": a box, behind the open list. */
    UIDropdownPart_List,
    /** @brief "list_padding": horizontal and vertical inside the list's box, UI pixels. */
    UIDropdownPart_ListPadding,
    /** @brief "row_height": a row's height, UI pixels. */
    UIDropdownPart_RowHeight,
    /** @brief "row": a box per row; hovered is the highlighted row, selected the chosen one. */
    UIDropdownPart_Row,
    /** @brief "row_text": a colour per state, a row's text. */
    UIDropdownPart_RowText,
    /** @brief "scroll_arrow_height": the height of the strips at the list's top and bottom that hold its scroll arrows, UI pixels. */
    UIDropdownPart_ScrollArrowHeight,
    /** @brief The number of a dropdown's values. */
    UIDropdownPart_Count
} UIDropdownPart;

/** @brief A menu's values in a theme. */
typedef enum UIMenuPartEnum
{
    /** @brief "padding": horizontal and vertical inside the menu's box, UI pixels. */
    UIMenuPart_Padding,
    /** @brief "item_height": an item's height, UI pixels. */
    UIMenuPart_ItemHeight,
    /** @brief "item_padding": horizontal and vertical inside an item, UI pixels. */
    UIMenuPart_ItemPadding,
    /** @brief "check_width": the column at an item's left that holds its check or its icon, UI pixels. */
    UIMenuPart_CheckWidth,
    /** @brief "separator_height": a separator's height, UI pixels. */
    UIMenuPart_SeparatorHeight,
    /** @brief "separator_thickness": the separator's line, UI pixels. */
    UIMenuPart_SeparatorThickness,
    /** @brief "background": a box, the menu. */
    UIMenuPart_Background,
    /** @brief "item": a box per item; hovered and focused are the item under the pointer or the keyboard, selected the one whose
     *  submenu is open. */
    UIMenuPart_Item,
    /** @brief "text": a colour per state, an item's label, check and submenu arrow. */
    UIMenuPart_Text,
    /** @brief "shortcut": a colour per state, an item's shortcut text. */
    UIMenuPart_Shortcut,
    /** @brief "separator": a box, the separator's line. */
    UIMenuPart_Separator,
    /** @brief The number of a menu's values. */
    UIMenuPart_Count
} UIMenuPart;

/** @brief A menu bar's values in a theme. */
typedef enum UIMenuBarPartEnum
{
    /** @brief "padding": horizontal and vertical inside the bar, UI pixels. */
    UIMenuBarPart_Padding,
    /** @brief "entry_padding": horizontal and vertical around an entry's text, UI pixels. */
    UIMenuBarPart_EntryPadding,
    /** @brief "min_size": UI pixels. */
    UIMenuBarPart_MinSize,
    /** @brief "background": a box, the bar. */
    UIMenuBarPart_Background,
    /** @brief "entry": a box per entry; hovered under the pointer, selected while its menu is open. */
    UIMenuBarPart_Entry,
    /** @brief "text": a colour per state, an entry's text. */
    UIMenuBarPart_Text,
    /** @brief The number of a menu bar's values. */
    UIMenuBarPart_Count
} UIMenuBarPart;

/** @brief A status bar's values in a theme. */
typedef enum UIStatusBarPartEnum
{
    /** @brief "padding": horizontal and vertical inside the bar, UI pixels. */
    UIStatusBarPart_Padding,
    /** @brief "spacing": between its sections' elements, UI pixels. */
    UIStatusBarPart_Spacing,
    /** @brief "min_size": UI pixels. */
    UIStatusBarPart_MinSize,
    /** @brief "background": a box, the bar. */
    UIStatusBarPart_Background,
    /** @brief "text": a colour per state, the message. */
    UIStatusBarPart_Text,
    /** @brief The number of a status bar's values. */
    UIStatusBarPart_Count
} UIStatusBarPart;

/** @brief A modal popup's backdrop's values in a theme. */
typedef enum UIBackdropPartEnum
{
    /** @brief "background": a box over the whole surface behind the popup (a translucent colour dims what lies behind). */
    UIBackdropPart_Background,
    /** @brief The number of a backdrop's values. */
    UIBackdropPart_Count
} UIBackdropPart;

/** @brief A text box's values in a theme. */
typedef enum UITextBoxPartEnum
{
    /** @brief "padding": between the box's edges and its text, horizontal and vertical, UI pixels. */
    UITextBoxPart_Padding,
    /** @brief "min_size": UI pixels; the box is as tall as its lines at least. */
    UITextBoxPart_MinSize,
    /** @brief "caret_width": how wide the caret's line is, UI pixels. */
    UITextBoxPart_CaretWidth,
    /** @brief "mask": the character a masked box shows for each of its own. */
    UITextBoxPart_Mask,
    /** @brief "box": a box behind a writable box's text. */
    UITextBoxPart_Box,
    /** @brief "read_only_box": a box behind a read-only box's text, so the two are told apart. */
    UITextBoxPart_ReadOnlyBox,
    /** @brief "selection": a box behind the selected text while the box is being edited. */
    UITextBoxPart_Selection,
    /** @brief "inactive_selection": a box behind the selected text while it is not. */
    UITextBoxPart_InactiveSelection,
    /** @brief "caret": a box, the caret's line. */
    UITextBoxPart_Caret,
    /** @brief "overwrite_caret": a box, the caret in overwrite mode: a block over the character it would replace. */
    UITextBoxPart_OverwriteCaret,
    /** @brief "text": colors. */
    UITextBoxPart_Text,
    /** @brief "placeholder": colors, the text shown while the box is empty. */
    UITextBoxPart_Placeholder,
    /** @brief The number of a text box's values. */
    UITextBoxPart_Count
} UITextBoxPart;

/** @brief A property box's values in a theme. */
typedef enum UIPropertyBoxPartEnum
{
    /** @brief "padding": between the box's edges and its text, horizontal and vertical, UI pixels. */
    UIPropertyBoxPart_Padding,
    /** @brief "min_size": UI pixels; the box is as tall as a line at least. */
    UIPropertyBoxPart_MinSize,
    /** @brief "caret_width": how wide the caret's line is while typing, UI pixels. */
    UIPropertyBoxPart_CaretWidth,
    /** @brief "unit_spacing": between a number and its unit, UI pixels. */
    UIPropertyBoxPart_UnitSpacing,
    /** @brief "box": a box behind everything. */
    UIPropertyBoxPart_Box,
    /** @brief "fill": a box behind the number from the box's start as far as the value's share of its range (a number that shows it). */
    UIPropertyBoxPart_Fill,
    /** @brief "selection": a box behind the selected text while typing. */
    UIPropertyBoxPart_Selection,
    /** @brief "caret": a box, the caret's line while typing. */
    UIPropertyBoxPart_Caret,
    /** @brief "text": colors, the number or the string. */
    UIPropertyBoxPart_Text,
    /** @brief "unit": colors, the unit after a number. */
    UIPropertyBoxPart_Unit,
    /** @brief The number of a property box's values. */
    UIPropertyBoxPart_Count
} UIPropertyBoxPart;

/** @brief A labelled element's values in a theme (its label is a label's). */
typedef enum UILabelledPartEnum
{
    /** @brief "spacing": between the label and the element, UI pixels. */
    UILabelledPart_Spacing,
    /** @brief The number of a labelled element's values. */
    UILabelledPart_Count
} UILabelledPart;

/** @brief A form's values in a theme. */
typedef enum UIFormPartEnum
{
    /** @brief "padding": around its rows, horizontal and vertical, UI pixels. */
    UIFormPart_Padding,
    /** @brief "spacing": between its rows, UI pixels. */
    UIFormPart_Spacing,
    /** @brief The number of a form's values. */
    UIFormPart_Count
} UIFormPart;

/** @brief A compound slider's values in a theme (its parts are a label's, a slider's and a property box's). */
typedef enum UICompoundSliderPartEnum
{
    /** @brief "spacing": between its parts, UI pixels. */
    UICompoundSliderPart_Spacing,
    /** @brief "box_width": how wide the property box beside a slider across is, UI pixels. */
    UICompoundSliderPart_BoxWidth,
    /** @brief The number of a compound slider's values. */
    UICompoundSliderPart_Count
} UICompoundSliderPart;

/** @brief A row of fields' values in a theme (its fields are property boxes). */
typedef enum UIVectorRowPartEnum
{
    /** @brief "spacing": between one field and the next tag, UI pixels. */
    UIVectorRowPart_Spacing,
    /** @brief "tag_width": how wide an axis tag is, UI pixels. */
    UIVectorRowPart_TagWidth,
    /** @brief "x_tag": a box, the first axis's tag. */
    UIVectorRowPart_XTag,
    /** @brief "y_tag": a box, the second axis's tag. */
    UIVectorRowPart_YTag,
    /** @brief "z_tag": a box, the third axis's tag. */
    UIVectorRowPart_ZTag,
    /** @brief "w_tag": a box, the fourth axis's tag. */
    UIVectorRowPart_WTag,
    /** @brief "tag_text": colors, the axis's letter on its tag. */
    UIVectorRowPart_TagText,
    /** @brief The number of a row of fields' values. */
    UIVectorRowPart_Count
} UIVectorRowPart;

/** @brief A list's values in a theme (its bar is a scroll bar's). */
typedef enum UIListPartEnum
{
    /** @brief "padding": inside the list's box, around its rows, horizontal and vertical, UI pixels. */
    UIListPart_Padding,
    /** @brief "row_height": a row's height, UI pixels. */
    UIListPart_RowHeight,
    /** @brief "row_padding": a row's inset at its sides and the gaps between its icon, its text and its cells, UI pixels. */
    UIListPart_RowPadding,
    /** @brief "cell_size": a toggle cell's side, UI pixels. */
    UIListPart_CellSize,
    /** @brief "insertion_thickness": how thick the line where a drop would go is, UI pixels. */
    UIListPart_InsertionThickness,
    /** @brief "background": a box, the list. */
    UIListPart_Background,
    /** @brief "row": a box per row; hovered under the pointer, selected while selected, disabled while the list is. */
    UIListPart_Row,
    /** @brief "current": a box drawn over the current row (where the keys act) while the list has the focus; entered while it is entered. */
    UIListPart_Current,
    /** @brief "text": a colour per state, a row's text and icon. */
    UIListPart_Text,
    /** @brief "cell": a box per toggle cell; selected while on, hovered under the pointer. */
    UIListPart_Cell,
    /** @brief "cell_text": a colour per state, a toggle cell's letter. */
    UIListPart_CellText,
    /** @brief "insertion": a box, the line where a drop would go. */
    UIListPart_Insertion,
    /** @brief The number of a list's values. */
    UIListPart_Count
} UIListPart;

/** @brief A tree's values in a theme (its bar is a scroll bar's). */
typedef enum UITreePartEnum
{
    /** @brief "padding": inside the tree's box, around its rows, horizontal and vertical, UI pixels. */
    UITreePart_Padding,
    /** @brief "row_height": a row's height, UI pixels. */
    UITreePart_RowHeight,
    /** @brief "row_padding": a row's inset at its sides and the gaps between its expander, icon, text and cells, UI pixels. */
    UITreePart_RowPadding,
    /** @brief "indent": how far each level of nesting moves a row's content in, UI pixels; the expander is as wide. */
    UITreePart_Indent,
    /** @brief "cell_size": a toggle cell's side, UI pixels. */
    UITreePart_CellSize,
    /** @brief "insertion_thickness": how thick the line where a drop would go is, UI pixels. */
    UITreePart_InsertionThickness,
    /** @brief "background": a box, the tree. */
    UITreePart_Background,
    /** @brief "row": a box per row; hovered under the pointer, selected while selected, disabled while the tree is. */
    UITreePart_Row,
    /** @brief "current": a box drawn over the current row while the tree has the focus; entered while it is entered. */
    UITreePart_Current,
    /** @brief "text": a colour per state, a row's text and icon. */
    UITreePart_Text,
    /** @brief "expander": a colour per state, the arrow of a node with children (pointing right while collapsed, down while expanded). */
    UITreePart_Expander,
    /** @brief "cell": a box per toggle cell; selected while on, hovered under the pointer. */
    UITreePart_Cell,
    /** @brief "cell_text": a colour per state, a toggle cell's letter. */
    UITreePart_CellText,
    /** @brief "insertion": a box, the line where a drop would go; for a drop into a row, the same line along the row's four edges. */
    UITreePart_Insertion,
    /** @brief The number of a tree's values. */
    UITreePart_Count
} UITreePart;

/** @brief A dialog's values in a theme (its title and message are labels, its buttons buttons). */
typedef enum UIDialogPartEnum
{
    /** @brief "padding": inside the dialog's box, horizontal and vertical, UI pixels. */
    UIDialogPart_Padding,
    /** @brief "spacing": between the title, the message, the content and the buttons, UI pixels. */
    UIDialogPart_Spacing,
    /** @brief "button_spacing": between two buttons, UI pixels. */
    UIDialogPart_ButtonSpacing,
    /** @brief "button_width": the least width of a button, UI pixels. */
    UIDialogPart_ButtonWidth,
    /** @brief "min_size": UI pixels. */
    UIDialogPart_MinSize,
    /** @brief "max_width": the widest the dialog grows to fit its message, which wraps past it, UI pixels. */
    UIDialogPart_MaxWidth,
    /** @brief "background": a box, the dialog. */
    UIDialogPart_Background,
    /** @brief The number of a dialog's values. */
    UIDialogPart_Count
} UIDialogPart;

/** @brief A colour picker's values in a theme: its swatch, and the popup it opens (whose fields are property boxes and a text box). */
typedef enum UIColorPickerPartEnum
{
    /** @brief "min_size": the swatch's least size, UI pixels. */
    UIColorPickerPart_MinSize,
    /** @brief "checker_size": the side of the squares a colour with alpha is drawn over, UI pixels. */
    UIColorPickerPart_CheckerSize,
    /** @brief "padding": inside the popup's box, horizontal and vertical, UI pixels. */
    UIColorPickerPart_Padding,
    /** @brief "spacing": between the popup's parts, UI pixels. */
    UIColorPickerPart_Spacing,
    /** @brief "square_size": the side of the square of saturation (across) and value (up), UI pixels. */
    UIColorPickerPart_SquareSize,
    /** @brief "strip_width": how wide the hue strip and the alpha strip are, UI pixels. */
    UIColorPickerPart_StripWidth,
    /** @brief "field_width": how wide the channels' fields are, UI pixels. */
    UIColorPickerPart_FieldWidth,
    /** @brief "swatch": a box drawn over the colour, the swatch's frame; pressed while its popup is open. */
    UIColorPickerPart_Swatch,
    /** @brief "checker_light": a colour, the lighter squares behind a colour with alpha. */
    UIColorPickerPart_CheckerLight,
    /** @brief "checker_dark": a colour, the darker squares. */
    UIColorPickerPart_CheckerDark,
    /** @brief "mixed": a colour per state, the hyphen the swatch shows while several colours differ. */
    UIColorPickerPart_Mixed,
    /** @brief "popup": a box, behind the popup. */
    UIColorPickerPart_Popup,
    /** @brief "frame": a box around the square and each strip; hovered under the pointer, pressed while dragged. */
    UIColorPickerPart_Frame,
    /** @brief "marker": a box, where the colour lies on the square. */
    UIColorPickerPart_Marker,
    /** @brief "strip_marker": a box, where the hue or the alpha lies on its strip. */
    UIColorPickerPart_StripMarker,
    /** @brief The number of a colour picker's values. */
    UIColorPickerPart_Count
} UIColorPickerPart;

/** @brief A tool bar's values in a theme (its buttons are buttons, of the variant its options name). */
typedef enum UIToolbarPartEnum
{
    /** @brief "padding": inside the bar, horizontal and vertical, UI pixels. */
    UIToolbarPart_Padding,
    /** @brief "spacing": between two buttons, UI pixels. */
    UIToolbarPart_Spacing,
    /** @brief "button_size": each button's size, UI pixels. */
    UIToolbarPart_ButtonSize,
    /** @brief "separator_thickness": how thick a separator's line is, UI pixels. */
    UIToolbarPart_SeparatorThickness,
    /** @brief "separator_spacing": the room a separator takes along the bar, its line in the middle, UI pixels. */
    UIToolbarPart_SeparatorSpacing,
    /** @brief "background": a box, the bar. */
    UIToolbarPart_Background,
    /** @brief "separator": a box, a separator's line. */
    UIToolbarPart_Separator,
    /** @brief The number of a tool bar's values. */
    UIToolbarPart_Count
} UIToolbarPart;

/** @brief What a drag shows, in a theme. */
typedef enum UIDragPartEnum
{
    /** @brief "padding": around the dragged item's text, horizontal and vertical, UI pixels. */
    UIDragPart_Padding,
    /** @brief "offset": where the dragged item's top-left sits from the pointer, UI pixels; negative components put it left of or above
     *  the pointer. */
    UIDragPart_Offset,
    /** @brief "max_width": the widest the dragged item is; its text is cut past it, UI pixels. */
    UIDragPart_MaxWidth,
    /** @brief "ghost": a box, the dragged item; selected while over a target that would take it, disabled over one that would not. */
    UIDragPart_Ghost,
    /** @brief "text": a colour per state, the dragged item's text. */
    UIDragPart_Text,
    /** @brief "target": a box drawn over the target that would take the drop, inside the area it is drawn in. */
    UIDragPart_Target,
    /** @brief The number of what a drag shows. */
    UIDragPart_Count
} UIDragPart;

/** @brief A window's values in a theme (its caption buttons are buttons of the "window_button" and "window_close" variants, its content
 *  a scroll area's). */
typedef enum UIWindowPartEnum
{
    /** @brief "title_height": the title bar's height, UI pixels. */
    UIWindowPart_TitleHeight,
    /** @brief "title_padding": from the title bar's left to the icon, and from its right to the last caption button, UI pixels. */
    UIWindowPart_TitlePadding,
    /** @brief "icon_spacing": between the icon and the title, UI pixels. */
    UIWindowPart_IconSpacing,
    /** @brief "button_size": a caption button's size, UI pixels. */
    UIWindowPart_ButtonSize,
    /** @brief "button_spacing": between two caption buttons, and between the title and the first of them, UI pixels. */
    UIWindowPart_ButtonSpacing,
    /** @brief "border": the frame around the content, at its left, right and bottom, UI pixels. */
    UIWindowPart_Border,
    /** @brief "resize_margin": how far in from each edge a press resizes the window, UI pixels. */
    UIWindowPart_ResizeMargin,
    /** @brief "min_size": the least size of a window whatever it holds, UI pixels. */
    UIWindowPart_MinSize,
    /** @brief "minimized_width": how wide a minimized window's title bar is, UI pixels. */
    UIWindowPart_MinimizedWidth,
    /** @brief "content_padding": inside the content's column, horizontal and vertical, UI pixels. */
    UIWindowPart_ContentPadding,
    /** @brief "content_spacing": between the content's elements, UI pixels. */
    UIWindowPart_ContentSpacing,
    /** @brief "frame": a box, the whole window; focused while the window is active (the focus is in it). */
    UIWindowPart_Frame,
    /** @brief "title_bar": a box, the title bar; focused while the window is active. */
    UIWindowPart_TitleBar,
    /** @brief "title_text": a colour per state, the title and a letter icon; focused while the window is active. */
    UIWindowPart_TitleText,
    /** @brief "content_background": a box behind the content; focused while the window is active. */
    UIWindowPart_ContentBackground,
    /** @brief "resize_edge": a box over an edge's band, hovered under the pointer and pressed while it is dragged; drawn only then. */
    UIWindowPart_ResizeEdge,
    /** @brief The number of a window's values. */
    UIWindowPart_Count
} UIWindowPart;

/** @brief A workspace's values in a theme: its dock's (its bars are a menu bar's and a status bar's, its windows windows'). */
typedef enum UIWorkspacePartEnum
{
    /** @brief "tab_height": a tab strip's height, UI pixels. */
    UIWorkspacePart_TabHeight,
    /** @brief "tab_padding": inside a tab, at its left and right, UI pixels. */
    UIWorkspacePart_TabPadding,
    /** @brief "tab_spacing": between two tabs, and before the first, UI pixels. */
    UIWorkspacePart_TabSpacing,
    /** @brief "icon_spacing": between a selected tab's icon and its name, UI pixels. */
    UIWorkspacePart_IconSpacing,
    /** @brief "tab_max_width": the widest a selected tab gets with its name, UI pixels. */
    UIWorkspacePart_TabMaxWidth,
    /** @brief "splitter_thickness": a splitter's thickness between two docking areas, UI pixels. */
    UIWorkspacePart_SplitterThickness,
    /** @brief "guide_size": the side of one of the five drop guides a docking area shows under a dragged window, UI pixels. */
    UIWorkspacePart_GuideSize,
    /** @brief "background": a box over the dock where no docking area is (an empty dock). */
    UIWorkspacePart_Background,
    /** @brief "strip": a box behind a docking area's tabs; focused while its shown window is active. */
    UIWorkspacePart_Strip,
    /** @brief "tab": a box, one tab: selected for its area's shown window, hovered under the pointer, pressed while held. */
    UIWorkspacePart_Tab,
    /** @brief "tab_text": a colour per state, a tab's icon and name. */
    UIWorkspacePart_TabText,
    /** @brief "splitter": a box between two docking areas, hovered under the pointer and pressed while it is dragged. */
    UIWorkspacePart_Splitter,
    /** @brief "zone": a box, one of the five drop guides of the docking area under a window being dragged (its centre's, adding a tab, and
     *  one beside it toward each edge, splitting the area there); hovered for the one under the pointer. */
    UIWorkspacePart_Zone,
    /** @brief "drop_preview": a box over where the dragged window would go if let go there. */
    UIWorkspacePart_DropPreview,
    /** @brief The number of a workspace's values. */
    UIWorkspacePart_Count
} UIWorkspacePart;

/** @brief A render view's values in a theme. */
typedef enum UIRenderViewPartEnum
{
    /** @brief "min_size": the least size of a view, UI pixels. */
    UIRenderViewPart_MinSize,
    /** @brief "background": a box filling the view while its host shows no picture in it. */
    UIRenderViewPart_Background,
    /** @brief "frame": a box drawn over the picture's edge: hovered under the pointer, focused while the view has the focus, entered
     *  while its host has the keys. */
    UIRenderViewPart_Frame,
    /** @brief The number of a render view's values. */
    UIRenderViewPart_Count
} UIRenderViewPart;

/**
 * @brief What a value of an element kind is.
 */
typedef enum UIThemePartKindEnum
{
    /** @brief A box style. */
    UIThemePartKind_Box,
    /** @brief A colour per state. */
    UIThemePartKind_Colors,
    /** @brief Two numbers (an array or a compound), each within the part's bounds. */
    UIThemePartKind_Vector,
    /** @brief One number within the part's bounds. */
    UIThemePartKind_Number,
    /** @brief One character: a string of exactly one codepoint, no control character. */
    UIThemePartKind_Character
} UIThemePartKind;

/**
 * @brief One value an element kind has in a theme: its key, what it is and, for a metric, its bounds and the value a theme that
 *        leaves it out gets. Static data.
 */
typedef struct UIThemePartDescriptorStruct
{
    /** @brief The key in the element's block. */
    const unsigned char* Key;
    /** @brief What it is. */
    UIThemePartKind Kind;
    /** @brief The default of a UIThemePartKind_Vector. */
    Vector2 DefaultVector;
    /** @brief The default of a UIThemePartKind_Number. */
    float DefaultNumber;
    /** @brief The smallest a UIThemePartKind_Number, or each component of a UIThemePartKind_Vector, may be: 0 for a size, a
     *  padding or a spacing, -UI_THEME_MAX_METRIC for a value that may point either way (the tooltip's offset). */
    float Minimum;
    /** @brief The largest such a value may be: UI_THEME_MAX_METRIC. */
    float Maximum;
    /** @brief The default of a UIThemePartKind_Character, a codepoint. */
    uint32_t DefaultCharacter;
} UIThemePartDescriptor;


// The element kinds.
/**
 * @brief Returns an element kind's key in a theme's "elements".
 * @param kind The kind.
 * @returns The static name, or NULL for a value out of range.
 */
const unsigned char* UIElementKind_GetName(UIElementKind kind);

/**
 * @brief Returns how many values an element kind has.
 * @param kind The kind.
 * @returns The count; 0 for a value out of range.
 */
size_t UIElementKind_GetPartCount(UIElementKind kind);

/**
 * @brief Returns the description of one of an element kind's values.
 * @param kind The kind.
 * @param part The value's index: one of that kind's part enum.
 * @returns The static description, or NULL when out of range.
 */
const UIThemePartDescriptor* UIElementKind_GetPart(UIElementKind kind, int32_t part);


// The theme.
/**
 * @brief Returns the theme's asset name.
 * @param self The theme; must not be NULL.
 * @returns The name; valid while the theme is.
 */
const unsigned char* UITheme_GetName(const UITheme* self);

/**
 * @brief Returns the theme's font's asset name.
 * @param self The theme, or NULL.
 * @returns The name, or NULL for no theme, a theme that names none or a font that did not load: text then draws in raylib's
 *          built-in font. Valid while the theme is.
 */
const unsigned char* UITheme_GetFontName(const UITheme* self);

/**
 * @brief Returns the theme's text size.
 * @param self The theme, or NULL.
 * @returns The line height in UI pixels; UI_THEME_DEFAULT_TEXT_SIZE for no theme.
 */
float UITheme_GetTextSize(const UITheme* self);

/**
 * @brief Returns how long the elements' colours take to fade from state to state.
 * @param self The theme, or NULL.
 * @returns The time in seconds; 0 (at once) for no theme.
 */
float UITheme_GetTransitionSeconds(const UITheme* self);

/**
 * @brief Returns how long the pointer rests on an element before its hover text shows.
 * @param self The theme, or NULL.
 * @returns The delay in seconds, within [0, UI_THEME_MAX_TOOLTIP_DELAY_SECONDS]; UI_THEME_DEFAULT_TOOLTIP_DELAY_SECONDS for
 *          no theme or a theme that leaves it out.
 */
float UITheme_GetTooltipDelaySeconds(const UITheme* self);

/**
 * @brief Returns the name of the look a theme gives an element kind: the registered code that draws the kind's elements.
 *
 * The name is a full namespaced key (a name written without a namespace is the default namespace's: "round" is
 * "tbp:round"). The theme only carries it: whether a look of that name exists is for the code that draws to find out, and
 * a theme loads either way. Only a kind's own block names a look; its variants draw with it.
 * @param self The theme, or NULL.
 * @param kind The kind.
 * @returns The name, or NULL for no theme, a kind out of range, a kind the theme leaves out or one that names no look.
 *          Valid while the theme is.
 */
const unsigned char* UITheme_GetLookName(const UITheme* self, UIElementKind kind);

/**
 * @brief Finds the values of an element kind, or of one of its variants.
 * @param self The theme, or NULL.
 * @param kind The kind.
 * @param variant The variant's name, or NULL or empty for the kind's own values. An unknown variant gets the kind's own.
 * @returns The style, or NULL when there is no theme or it leaves the kind out. Valid while the theme is.
 */
const UIThemeStyle* UITheme_FindStyle(const UITheme* self, UIElementKind kind, const unsigned char* variant);

/**
 * @brief Returns a box part of a style.
 * @param self The style, or NULL.
 * @param part The part's index; a part that is not a box counts as left out.
 * @returns The box, or NULL when left out.
 */
const UIBoxStyle* UIThemeStyle_GetBox(const UIThemeStyle* self, int32_t part);

/**
 * @brief Returns a colours part of a style.
 * @param self The style, or NULL.
 * @param part The part's index; a part that is not colours counts as left out.
 * @returns The colours, or NULL when left out.
 */
const UIStateColors* UIThemeStyle_GetColors(const UIThemeStyle* self, int32_t part);

/**
 * @brief Returns a vector metric of a style.
 * @param self The style, or NULL.
 * @param kind The style's element kind (for the default).
 * @param part The part's index.
 * @returns The metric, or the kind's default when left out or @p self is NULL; zero when @p part is no vector.
 */
Vector2 UIThemeStyle_GetVector(const UIThemeStyle* self, UIElementKind kind, int32_t part);

/**
 * @brief Returns a number metric of a style.
 * @param self The style, or NULL.
 * @param kind The style's element kind (for the default).
 * @param part The part's index.
 * @returns The metric, or the kind's default when left out or @p self is NULL; zero when @p part is no number.
 */
float UIThemeStyle_GetNumber(const UIThemeStyle* self, UIElementKind kind, int32_t part);

/**
 * @brief Returns a character part of a style.
 * @param self The style, or NULL.
 * @param kind The style's element kind (for the default).
 * @param part The part's index.
 * @returns The character's codepoint, or the kind's default when left out or @p self is NULL; 0 when @p part is no character.
 */
uint32_t UIThemeStyle_GetCharacter(const UIThemeStyle* self, UIElementKind kind, int32_t part);

/**
 * @brief Finds a custom number of a style: a value its "custom" block (or, for a variant, its kind's) wrote as a JSON number.
 *
 * Custom values are for looks whose visuals the kind's parts do not describe; the look decides their keys and their
 * defaults. A linear search over at most UI_THEME_MAX_CUSTOM_VALUES keys.
 * @param self The style, or NULL.
 * @param key The value's key, or NULL.
 * @param defaultValue What to answer when there is no such number.
 * @returns The number, within [-UI_THEME_MAX_CUSTOM_NUMBER, UI_THEME_MAX_CUSTOM_NUMBER]; @p defaultValue when @p self or
 *          @p key is NULL, the key is absent or it holds a value of another kind.
 */
float UIThemeStyle_FindCustomNumber(const UIThemeStyle* self, const unsigned char* key, float defaultValue);

/**
 * @brief Finds a custom vector of a style: a value written as an array of two numbers.
 * @param self The style, or NULL.
 * @param key The value's key, or NULL.
 * @param defaultValue What to answer when there is no such vector.
 * @returns The vector, each component within [-UI_THEME_MAX_CUSTOM_NUMBER, UI_THEME_MAX_CUSTOM_NUMBER]; @p defaultValue
 *          when @p self or @p key is NULL, the key is absent or it holds a value of another kind.
 */
Vector2 UIThemeStyle_FindCustomVector(const UIThemeStyle* self, const unsigned char* key, Vector2 defaultValue);

/**
 * @brief Finds a custom colour of a style: a value written as a string ("#rrggbb", "#rrggbbaa", or "@name" from the
 *        theme's palette, resolved when the theme was read).
 * @param self The style, or NULL.
 * @param key The value's key, or NULL.
 * @param defaultValue What to answer when there is no such colour.
 * @returns The colour; @p defaultValue when @p self or @p key is NULL, the key is absent or it holds a value of another
 *          kind.
 */
Color UIThemeStyle_FindCustomColor(const UIThemeStyle* self, const unsigned char* key, Color defaultValue);

/**
 * @brief Finds a custom box of a style: a value written as a compound, read as a box part is (its nine-slice texture
 *        loaded with the theme).
 * @param self The style, or NULL.
 * @param key The value's key, or NULL.
 * @returns The box, valid while the theme is; NULL when @p self or @p key is NULL, the key is absent or it holds a value
 *          of another kind.
 */
const UIBoxStyle* UIThemeStyle_FindCustomBox(const UIThemeStyle* self, const unsigned char* key);
