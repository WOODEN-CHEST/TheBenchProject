#include "GameInput.h"
#include "GameInputInternal.h"
#include "GameKeyInternal.h"
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include "wr/WRChar.h"
#include "wr/WRFileSystem.h"
#include "wr/WRJSON.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"
#include "wr/WRUnicode.h"
#include "wr/WRUserData.h"
#include "raylib/raylib.h"


// Macros.
/** JSON key of the object that maps action keys to binding lists in a controls file. */
#define CONTROLS_KEY_BINDINGS ((const unsigned char*)u8"bindings")
/** Joins the modifiers and the key of a binding's text form. */
#define BINDING_SEPARATOR ((unsigned char)'+')
/** Initial capacity (bytes) for a controls file's contents; the buffer grows as needed. */
#define CONTROLS_FILE_INITIAL_CAPACITY ((size_t)1024)
/** Length of the UTF-8 byte order mark some editors put at the start of a text file. */
#define UTF8_BOM_LENGTH ((size_t)3)
/** The source of the engine's own registrations. */
#define ENGINE_SOURCE NAMESPACED_KEY_ID_NONE
/** Bytes of one formatted log line (longer lines are cut). */
#define LOG_LINE_CAPACITY ((size_t)1024)
/** Bytes of one key's printable copy in a controls-file message (longer keys are cut). */
#define PRINTABLE_KEY_CAPACITY ((size_t)160)
/** Initial capacity (elements) of the per-action and per-category arrays and of the key index. */
#define INITIAL_ACTION_CAPACITY ((size_t)32)
/* Default-binding table entry builder. */
#define BINDING(key, modifiers) { .Key = (key), .Modifiers = (modifiers) }


// Types.
/* An engine category: registered first, in GameActionCategory order. */
typedef struct BuiltInCategoryStruct
{
    const unsigned char* Key;
    const unsigned char* DisplayName;
    const unsigned char* Description;
    int32_t Order;
} BuiltInCategory;

/* An engine action: registered after the categories, in GameAction order. */
typedef struct BuiltInActionStruct
{
    const unsigned char* Key;
    const unsigned char* DisplayName;
    const unsigned char* Description;
    GameActionCategory Category;
    GameInputBinding Defaults[GAME_INPUT_MAX_BINDINGS_PER_ACTION];
    size_t DefaultCount;
    bool KeepsABinding;
} BuiltInAction;

/* The action registry's element: an action as registered, with owned strings. */
typedef struct ActionEntryStruct
{
    unsigned char* DisplayName;
    unsigned char* Description;
    GameActionCategoryId Category;
    GameInputBinding DefaultBindings[GAME_INPUT_MAX_BINDINGS_PER_ACTION];
    size_t DefaultBindingCount;
    GameActionKind Kind;
    bool KeepsABinding;
} ActionEntry;

/* The category registry's element: a category as registered, with owned strings. */
typedef struct CategoryEntryStruct
{
    unsigned char* DisplayName;
    unsigned char* Description;
    int32_t Order;
} CategoryEntry;

/* An action's override: its source and the parts it changes, with owned strings. */
typedef struct ActionOverrideStruct
{
    uint32_t Source;
    bool OverridesDisplayName;
    unsigned char* DisplayName;
    bool OverridesDescription;
    unsigned char* Description;
    bool OverridesDefaultBindings;
    GameInputBinding DefaultBindings[GAME_INPUT_MAX_BINDINGS_PER_ACTION];
    size_t DefaultBindingCount;
} ActionOverride;

/* What the input keeps per action id beside the registry; it outlives withdrawals. */
typedef struct ActionStateStruct
{
    GameInputBinding Bindings[GAME_INPUT_MAX_BINDINGS_PER_ACTION];
    size_t BindingCount;
    bool IsSetByPlayer;
    bool HasOverride;
    ActionOverride Override;
} ActionState;

/* A category's override. */
typedef struct CategoryOverrideStruct
{
    uint32_t Source;
    bool OverridesDisplayName;
    unsigned char* DisplayName;
    bool OverridesDescription;
    unsigned char* Description;
    bool OverridesOrder;
    int32_t Order;
} CategoryOverride;

/* What the input keeps per category id beside the registry. */
typedef struct CategoryStateStruct
{
    bool HasOverride;
    CategoryOverride Override;
} CategoryState;

/* One binding in the key index; its key is its group's. */
typedef struct KeyIndexEntryStruct
{
    GameActionId Action;
    uint8_t Modifiers;
} KeyIndexEntry;

/* A controls-file entry: a full key and its bindings (kept for keys no action has, staged while a file is read). */
typedef struct ControlsEntryStruct
{
    unsigned char* Key;
    GameInputBinding Bindings[GAME_INPUT_MAX_BINDINGS_PER_ACTION];
    size_t BindingCount;
} ControlsEntry;

/* A modifier's name in a binding's text form. */
typedef struct ModifierNameStruct
{
    uint8_t Flag;
    const unsigned char* Name;
} ModifierName;


// Fields.
/* Indexed by GameActionCategory. The keys are stored in files: never rename one. */
static const BuiltInCategory BuiltInCategories[GameActionCategory_Count] =
{
    [GameActionCategory_Movement] = { .Key = u8"tbp:movement", .DisplayName = u8"Movement",
        .Description = u8"Walking, flying and the free camera.", .Order = 100 },
    [GameActionCategory_Character] = { .Key = u8"tbp:character", .DisplayName = u8"Character",
        .Description = u8"What a character does: jumping, crouching and using things.", .Order = 200 },
    [GameActionCategory_General] = { .Key = u8"tbp:general", .DisplayName = u8"General",
        .Description = u8"The game menu and other actions that work everywhere.", .Order = 300 },
    [GameActionCategory_Interface] = { .Key = u8"tbp:interface", .DisplayName = u8"Interface",
        .Description = u8"Moving through menus and other interfaces with the keyboard.", .Order = 400 },
};

/* Indexed by GameAction with designated initializers. The keys are stored in controls files: never rename one. */
static const BuiltInAction BuiltInActions[GameAction_Count] =
{
    [GameAction_UIUp] = { .Key = u8"tbp:ui_up", .DisplayName = u8"Menu Up",
        .Description = u8"Moves to the nearest item above.", .Category = GameActionCategory_Interface,
        .Defaults = { BINDING(GameKey_Up, GAME_KEY_MODIFIER_NONE) }, .DefaultCount = 1 },
    [GameAction_UIDown] = { .Key = u8"tbp:ui_down", .DisplayName = u8"Menu Down",
        .Description = u8"Moves to the nearest item below.", .Category = GameActionCategory_Interface,
        .Defaults = { BINDING(GameKey_Down, GAME_KEY_MODIFIER_NONE) }, .DefaultCount = 1 },
    [GameAction_UILeft] = { .Key = u8"tbp:ui_left", .DisplayName = u8"Menu Left",
        .Description = u8"Moves to the nearest item to the left.", .Category = GameActionCategory_Interface,
        .Defaults = { BINDING(GameKey_Left, GAME_KEY_MODIFIER_NONE) }, .DefaultCount = 1 },
    [GameAction_UIRight] = { .Key = u8"tbp:ui_right", .DisplayName = u8"Menu Right",
        .Description = u8"Moves to the nearest item to the right.", .Category = GameActionCategory_Interface,
        .Defaults = { BINDING(GameKey_Right, GAME_KEY_MODIFIER_NONE) }, .DefaultCount = 1 },
    [GameAction_UINext] = { .Key = u8"tbp:ui_next", .DisplayName = u8"Next",
        .Description = u8"Moves to the next item in reading order.", .Category = GameActionCategory_Interface,
        .Defaults = { BINDING(GameKey_Tab, GAME_KEY_MODIFIER_NONE) }, .DefaultCount = 1 },
    [GameAction_UIPrevious] = { .Key = u8"tbp:ui_previous", .DisplayName = u8"Previous",
        .Description = u8"Moves to the previous item in reading order.", .Category = GameActionCategory_Interface,
        .Defaults = { BINDING(GameKey_Tab, GAME_KEY_MODIFIER_SHIFT) }, .DefaultCount = 1 },
    [GameAction_UIAccept] = { .Key = u8"tbp:ui_accept", .DisplayName = u8"Accept",
        .Description = u8"Enters the focused group of items.", .Category = GameActionCategory_Interface,
        .Defaults = { BINDING(GameKey_Enter, GAME_KEY_MODIFIER_NONE), BINDING(GameKey_KeypadEnter, GAME_KEY_MODIFIER_NONE) },
        .DefaultCount = 2 },
    [GameAction_UIBack] = { .Key = u8"tbp:ui_back", .DisplayName = u8"Back",
        .Description = u8"Leaves the current group of items, or the menu.", .Category = GameActionCategory_Interface,
        .Defaults = { BINDING(GameKey_Escape, GAME_KEY_MODIFIER_NONE) }, .DefaultCount = 1 },
    [GameAction_MoveForward] = { .Key = u8"tbp:move_forward", .DisplayName = u8"Move Forward",
        .Description = u8"Walks or flies forward.", .Category = GameActionCategory_Movement,
        .Defaults = { BINDING(GameKey_W, GAME_KEY_MODIFIER_NONE) }, .DefaultCount = 1 },
    [GameAction_MoveBack] = { .Key = u8"tbp:move_back", .DisplayName = u8"Move Back",
        .Description = u8"Walks or flies backward.", .Category = GameActionCategory_Movement,
        .Defaults = { BINDING(GameKey_S, GAME_KEY_MODIFIER_NONE) }, .DefaultCount = 1 },
    [GameAction_MoveLeft] = { .Key = u8"tbp:move_left", .DisplayName = u8"Move Left",
        .Description = u8"Steps or flies to the left.", .Category = GameActionCategory_Movement,
        .Defaults = { BINDING(GameKey_A, GAME_KEY_MODIFIER_NONE) }, .DefaultCount = 1 },
    [GameAction_MoveRight] = { .Key = u8"tbp:move_right", .DisplayName = u8"Move Right",
        .Description = u8"Steps or flies to the right.", .Category = GameActionCategory_Movement,
        .Defaults = { BINDING(GameKey_D, GAME_KEY_MODIFIER_NONE) }, .DefaultCount = 1 },
    [GameAction_MoveUp] = { .Key = u8"tbp:move_up", .DisplayName = u8"Move Up",
        .Description = u8"Climbs while flying, and with the free camera.", .Category = GameActionCategory_Movement,
        .Defaults = { BINDING(GameKey_Space, GAME_KEY_MODIFIER_NONE) }, .DefaultCount = 1 },
    [GameAction_MoveDown] = { .Key = u8"tbp:move_down", .DisplayName = u8"Move Down",
        .Description = u8"Descends while flying, and with the free camera.", .Category = GameActionCategory_Movement,
        .Defaults = { BINDING(GameKey_LeftControl, GAME_KEY_MODIFIER_NONE) }, .DefaultCount = 1 },
    [GameAction_Sprint] = { .Key = u8"tbp:sprint", .DisplayName = u8"Sprint",
        .Description = u8"Moves faster while held.", .Category = GameActionCategory_Movement,
        .Defaults = { BINDING(GameKey_LeftShift, GAME_KEY_MODIFIER_NONE) }, .DefaultCount = 1 },
    [GameAction_Jump] = { .Key = u8"tbp:jump", .DisplayName = u8"Jump",
        .Description = u8"Jumps, when walking.", .Category = GameActionCategory_Character,
        .Defaults = { BINDING(GameKey_Space, GAME_KEY_MODIFIER_NONE) }, .DefaultCount = 1 },
    [GameAction_Crouch] = { .Key = u8"tbp:crouch", .DisplayName = u8"Crouch",
        .Description = u8"Crouches while held, when walking.", .Category = GameActionCategory_Character,
        .Defaults = { BINDING(GameKey_LeftControl, GAME_KEY_MODIFIER_NONE) }, .DefaultCount = 1 },
    [GameAction_Use] = { .Key = u8"tbp:use", .DisplayName = u8"Use",
        .Description = u8"Uses what the character looks at.", .Category = GameActionCategory_Character,
        .Defaults = { BINDING(GameKey_E, GAME_KEY_MODIFIER_NONE) }, .DefaultCount = 1 },
    [GameAction_Pause] = { .Key = u8"tbp:pause", .DisplayName = u8"Pause",
        .Description = u8"Opens the game menu.", .Category = GameActionCategory_General,
        .Defaults = { BINDING(GameKey_Escape, GAME_KEY_MODIFIER_NONE) }, .DefaultCount = 1, .KeepsABinding = true },
};

/* The modifiers in the order a binding's text form lists them. */
static const ModifierName ModifierNames[] =
{
    { .Flag = GAME_KEY_MODIFIER_CONTROL, .Name = u8"control" },
    { .Flag = GAME_KEY_MODIFIER_ALT, .Name = u8"alt" },
    { .Flag = GAME_KEY_MODIFIER_SHIFT, .Name = u8"shift" },
    { .Flag = GAME_KEY_MODIFIER_SUPER, .Name = u8"super" },
};

/* Tests only (GameInputInternal_GetMatchVisitCount): key-index entries the most-specific-binding check looked at. */
static uint64_t MatchVisitCount = 0U;


// Static functions: helpers.
static bool AreModifiersIn(uint8_t heldModifiers, uint8_t modifiers)
{
    return (heldModifiers & modifiers) == modifiers;
}

/* Reads one raylib key or mouse button into GAME_INPUT_KEY_STATE_* flags. */
static uint8_t SampleKey(GameKey key)
{
    int Code = (int)GameKey_GetRaylibCode(key);
    uint8_t State = 0;
    if (GameKey_IsMouseButton(key))
    {
        if (IsMouseButtonDown(Code)) { State |= GAME_INPUT_KEY_STATE_DOWN; }
        if (IsMouseButtonPressed(Code)) { State |= GAME_INPUT_KEY_STATE_PRESSED; }
        if (IsMouseButtonReleased(Code)) { State |= GAME_INPUT_KEY_STATE_RELEASED; }
        return State;
    }
    if (IsKeyDown(Code)) { State |= GAME_INPUT_KEY_STATE_DOWN; }
    if (IsKeyPressed(Code)) { State |= GAME_INPUT_KEY_STATE_PRESSED; }
    if (IsKeyReleased(Code)) { State |= GAME_INPUT_KEY_STATE_RELEASED; }
    if (IsKeyPressedRepeat(Code)) { State |= GAME_INPUT_KEY_STATE_REPEATED; }
    return State;
}

/* Orders two NUL-terminated texts by their bytes: below 0, 0 or above 0. */
static int CompareTexts(const unsigned char* a, const unsigned char* b)
{
    size_t Index = 0U;
    while ((a[Index] == b[Index]) && (a[Index] != (unsigned char)0))
    {
        Index++;
    }
    return (int)a[Index] - (int)b[Index];
}

static unsigned char* DuplicateText(const unsigned char* text)
{
    size_t Length = StringUTF8_GetByteLength(text);
    unsigned char* Copy = Memory_Allocate(Length + 1U);
    Memory_Copy(text, Copy, Length + 1U);
    return Copy;
}

/* What is wrong with a display name or a description, or NULL: present, within its length, UTF-8, no control
 * characters (they would reach the log and the menus). */
static const char* DescribeTextProblem(const unsigned char* text, bool isEmptyAllowed, size_t maxBytes)
{
    if (text == NULL)
    {
        return "is NULL";
    }
    size_t Length = StringUTF8_GetByteLength(text);
    if ((Length == 0U) && !isEmptyAllowed)
    {
        return "is empty";
    }
    if (Length > maxBytes)
    {
        return "is too long";
    }
    if (!StringUTF8_IsEncodingValid(text))
    {
        return "is not UTF-8";
    }
    for (size_t Index = 0U; Index < Length; Index++)
    {
        if ((text[Index] < 0x20U) || (text[Index] == 0x7FU))
        {
            return "holds a control character";
        }
    }
    return NULL;
}

static Error CheckDisplayName(const char* function, const unsigned char* text)
{
    const char* Problem = DescribeTextProblem(text, false, GAME_INPUT_MAX_DISPLAY_NAME_BYTES);
    if (Problem == NULL)
    {
        return Error_CreateSuccess();
    }
    return Error_Construct4(ErrorCode_IllegalArgument, "%s: the display name %s (1 to %" PRIu64
        " bytes of UTF-8 without control characters).", function, Problem, (uint64_t)GAME_INPUT_MAX_DISPLAY_NAME_BYTES);
}

static Error CheckDescription(const char* function, const unsigned char* text)
{
    const char* Problem = DescribeTextProblem(text, true, GAME_INPUT_MAX_DESCRIPTION_BYTES);
    if (Problem == NULL)
    {
        return Error_CreateSuccess();
    }
    return Error_Construct4(ErrorCode_IllegalArgument, "%s: the description %s (at most %" PRIu64
        " bytes of UTF-8 without control characters).", function, Problem, (uint64_t)GAME_INPUT_MAX_DESCRIPTION_BYTES);
}

/* Checks a list of default bindings: at most the maximum, each valid. */
static Error CheckDefaults(const char* function, const GameInputBinding* bindings, size_t count)
{
    if (count > GAME_INPUT_MAX_BINDINGS_PER_ACTION)
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange, "%s: %" PRIu64 " default bindings; at most %" PRIu64
            " are allowed.", function, (uint64_t)count, (uint64_t)GAME_INPUT_MAX_BINDINGS_PER_ACTION);
    }
    for (size_t Index = 0U; Index < count; Index++)
    {
        if (!GameInputBinding_IsValid(&bindings[Index]))
        {
            return Error_Construct4(ErrorCode_IllegalArgument, "%s: default binding %" PRIu64
                " has an invalid key or modifier.", function, (uint64_t)Index);
        }
    }
    return Error_CreateSuccess();
}

/* Who a source is, for messages: "the engine" or its key. */
static const char* DescribeSource(const GameInput* self, uint32_t sourceKeyId)
{
    if (sourceKeyId == ENGINE_SOURCE)
    {
        return "the engine";
    }
    const unsigned char* Text = KeyRegistry_GetText(self->_keys, sourceKeyId);
    return (Text != NULL) ? (const char*)Text : "an unknown source";
}

static bool IsSourceValid(const GameInput* self, uint32_t sourceKeyId)
{
    return (sourceKeyId == ENGINE_SOURCE) || (KeyRegistry_GetText(self->_keys, sourceKeyId) != NULL);
}

/* Logs one formatted line at a level; nothing without a logger. */
static void LogLine(const GameInput* self, LogLevel level, const char* format, ...)
{
    if (self->_logger == NULL)
    {
        return;
    }
    char Text[LOG_LINE_CAPACITY];
    va_list Arguments;
    va_start(Arguments, format);
    int Length = vsnprintf(Text, sizeof(Text), format, Arguments);
    va_end(Arguments);
    if (Length < 0)
    {
        return;
    }
    Error LogResult = Logger_LogFormatted(self->_logger, level, (const unsigned char*)u8"Input: %s", Text);
    Error_Deconstruct(&LogResult);
}

/* Copies text from a controls file into a buffer for a message, cut to fit, with every byte that is not printable ASCII
 * shown as '?' (a file cannot put control characters into the log). */
static void CopyPrintable(const unsigned char* text, size_t length, char* buffer, size_t capacity)
{
    size_t Count = (length < (capacity - 1U)) ? length : (capacity - 1U);
    for (size_t Index = 0U; Index < Count; Index++)
    {
        bool IsPrintable = (text[Index] >= 0x20U) && (text[Index] < 0x7FU);
        buffer[Index] = IsPrintable ? (char)text[Index] : '?';
    }
    buffer[Count] = '\0';
}


// Static functions: entries and states.
static ActionState* GetActionStates(const GameInput* self)
{
    return (ActionState*)self->_actionStates._data;
}

static CategoryState* GetCategoryStates(const GameInput* self)
{
    return (CategoryState*)self->_categoryStates._data;
}

/* An action's state, also while it is withdrawn; NULL for an unknown id. */
static ActionState* FindActionState(const GameInput* self, GameActionId id)
{
    return (id < self->_actionStates._count) ? &GetActionStates(self)[id] : NULL;
}

/* An active action's registration; NULL while it is withdrawn and for an unknown id. */
static const ActionEntry* GetActionEntry(const GameInput* self, GameActionId id)
{
    return Registry_Get(&self->_actions, id);
}

/* An active action's state; NULL while it is withdrawn and for an unknown id. */
static ActionState* FindActiveActionState(const GameInput* self, GameActionId id)
{
    return Registry_IsActive(&self->_actions, id) ? FindActionState(self, id) : NULL;
}

static const CategoryEntry* GetCategoryEntry(const GameInput* self, GameActionCategoryId id)
{
    return Registry_Get(&self->_categories, id);
}

static void FreeActionOverride(ActionOverride* override)
{
    Memory_Free(override->DisplayName);
    Memory_Free(override->Description);
    Memory_Zero(override, sizeof(*override));
}

static void FreeCategoryOverride(CategoryOverride* override)
{
    Memory_Free(override->DisplayName);
    Memory_Free(override->Description);
    Memory_Zero(override, sizeof(*override));
}

static void DeconstructActionEntry(void* element, const UserData* userData)
{
    UNUSED(userData);
    ActionEntry* Entry = element;
    Memory_Free(Entry->DisplayName);
    Memory_Free(Entry->Description);
}

static void DeconstructCategoryEntry(void* element, const UserData* userData)
{
    UNUSED(userData);
    CategoryEntry* Entry = element;
    Memory_Free(Entry->DisplayName);
    Memory_Free(Entry->Description);
}

/* Whether an action's override gives its defaults. Never empty ones for an action that keeps a binding: such an
 * override was refused, but one can sit on a withdrawn action that revives keeping a binding. */
static bool IsOverridingDefaults(const ActionEntry* entry, const ActionState* state)
{
    return state->HasOverride && state->Override.OverridesDefaultBindings
        && ((state->Override.DefaultBindingCount > 0U) || !entry->KeepsABinding);
}

/* An active action's effective defaults: its override's, else its registration's. */
static void GetEffectiveDefaults(const GameInput* self, GameActionId id, const GameInputBinding** outBindings,
    size_t* outCount)
{
    const ActionState* State = FindActionState(self, id);
    const ActionEntry* Entry = GetActionEntry(self, id);
    if (IsOverridingDefaults(Entry, State))
    {
        *outBindings = State->Override.DefaultBindings;
        *outCount = State->Override.DefaultBindingCount;
        return;
    }
    *outBindings = Entry->DefaultBindings;
    *outCount = Entry->DefaultBindingCount;
}

/* Gives an active action its effective defaults; it follows them from now on. */
static void RestoreDefaults(GameInput* self, GameActionId id)
{
    ActionState* State = FindActionState(self, id);
    const GameInputBinding* Defaults = NULL;
    size_t Count = 0U;
    GetEffectiveDefaults(self, id, &Defaults, &Count);
    if (Count > 0U)
    {
        Memory_Copy(Defaults, State->Bindings, sizeof(GameInputBinding) * Count);
    }
    State->BindingCount = Count;
    State->IsSetByPlayer = false;
}

/* Gives every active action the player did not set its effective defaults (after an override came or went). */
static void RefreshDefaults(GameInput* self)
{
    for (GameActionId Id = 0U; Id < Registry_GetCount(&self->_actions); Id++)
    {
        if (Registry_IsActive(&self->_actions, Id) && !FindActionState(self, Id)->IsSetByPlayer)
        {
            RestoreDefaults(self, Id);
        }
    }
}

/* Rebuilds the key index from the bindings of the active actions: counted per key, then placed. */
static void RebuildKeyIndex(GameInput* self)
{
    uint32_t Counts[GameKey_Count];
    Memory_Zero(Counts, sizeof(Counts));
    const ActionState* States = GetActionStates(self);
    GameActionId ActionCount = Registry_GetCount(&self->_actions);
    for (GameActionId Id = 0U; Id < ActionCount; Id++)
    {
        if (!Registry_IsActive(&self->_actions, Id))
        {
            continue;
        }
        for (size_t Index = 0U; Index < States[Id].BindingCount; Index++)
        {
            Counts[States[Id].Bindings[Index].Key]++;
        }
    }

    uint32_t Total = 0U;
    for (size_t Key = 0U; Key < (size_t)GameKey_Count; Key++)
    {
        self->_keyIndexStarts[Key] = Total;
        Total += Counts[Key];
    }
    self->_keyIndexStarts[GameKey_Count] = Total;
    GenericBuffer_EnsureTotalCapacity(&self->_keyIndex, Total);
    GenericBuffer_SetCount(&self->_keyIndex, Total);

    // Place each binding at its key's next free entry (Counts becomes the cursor).
    Memory_Copy(self->_keyIndexStarts, Counts, sizeof(Counts));
    KeyIndexEntry* Entries = (KeyIndexEntry*)self->_keyIndex._data;
    for (GameActionId Id = 0U; Id < ActionCount; Id++)
    {
        if (!Registry_IsActive(&self->_actions, Id))
        {
            continue;
        }
        for (size_t Index = 0U; Index < States[Id].BindingCount; Index++)
        {
            const GameInputBinding* Binding = &States[Id].Bindings[Index];
            Entries[Counts[Binding->Key]] = (KeyIndexEntry){ .Action = Id, .Modifiers = Binding->Modifiers };
            Counts[Binding->Key]++;
        }
    }
}

/* Whether a binding matches the held modifiers: every modifier it asks for is held, and no binding (of any active
 * action) on the same key asks for more of the held ones. The most specific binding wins, so Shift+Tab is not also a
 * plain Tab, whichever actions they are bound to. Only the key's own entries of the key index are looked at. */
static bool IsBindingMatched(const GameInput* self, const GameInputBinding* binding)
{
    uint8_t Held = GameInput_GetHeldModifiers(self);
    if (!AreModifiersIn(Held, binding->Modifiers))
    {
        return false;
    }
    const KeyIndexEntry* Entries = (const KeyIndexEntry*)self->_keyIndex._data;
    uint32_t End = self->_keyIndexStarts[(size_t)binding->Key + 1U];
    for (uint32_t Index = self->_keyIndexStarts[binding->Key]; Index < End; Index++)
    {
        MatchVisitCount++;
        uint8_t Other = Entries[Index].Modifiers;
        bool IsMoreSpecific = (Other != binding->Modifiers) && AreModifiersIn(Other, binding->Modifiers);
        if (IsMoreSpecific && AreModifiersIn(Held, Other))
        {
            return false;
        }
    }
    return true;
}

/* Whether any binding of an active action has its (unconsumed) key in one of the states in @p stateMask and matches
 * the held modifiers. */
static bool IsActionInState(const GameInput* self, GameActionId id, uint8_t stateMask)
{
    const ActionState* State = FindActiveActionState(self, id);
    if (State == NULL)
    {
        return false;
    }
    for (size_t Index = 0; Index < State->BindingCount; Index++)
    {
        const GameInputBinding* Binding = &State->Bindings[Index];
        if (((GameInput_GetKeyState(self, Binding->Key) & stateMask) != 0) && IsBindingMatched(self, Binding))
        {
            return true;
        }
    }
    return false;
}

/* Logs each of an action's effective defaults that another active action has bound too (the same key and modifiers),
 * naming both; the key index must be current. */
static void LogDefaultConflicts(const GameInput* self, GameActionId id)
{
    const GameInputBinding* Defaults = NULL;
    size_t Count = 0U;
    GetEffectiveDefaults(self, id, &Defaults, &Count);
    const KeyIndexEntry* Entries = (const KeyIndexEntry*)self->_keyIndex._data;
    for (size_t Index = 0U; Index < Count; Index++)
    {
        const GameInputBinding* Binding = &Defaults[Index];
        uint32_t End = self->_keyIndexStarts[(size_t)Binding->Key + 1U];
        for (uint32_t Entry = self->_keyIndexStarts[Binding->Key]; Entry < End; Entry++)
        {
            if ((Entries[Entry].Action == id) || (Entries[Entry].Modifiers != Binding->Modifiers))
            {
                continue;
            }
            unsigned char TextStorage[GAME_INPUT_MAX_BINDING_TEXT_BYTES + 1];
            GenericBuffer Text;
            GenericBuffer_CreateConstant(&Text, TextStorage, sizeof(TextStorage), sizeof(unsigned char), 0);
            Error TextResult = GameInputBinding_AppendText(Binding, &Text);
            Error_Deconstruct(&TextResult);
            LogLine(self, LogLevel_Warning, "\"%s\"'s default binding \"%s\" is also bound to \"%s\"; both keep it.",
                (const char*)GameInput_GetActionKeyText(self, id), (const char*)TextStorage,
                (const char*)GameInput_GetActionKeyText(self, Entries[Entry].Action));
        }
    }
}

/* Makes room for the per-action states up to the registry's slot count (new slots start zeroed). */
static void GrowActionStates(GameInput* self)
{
    size_t SlotCount = Registry_GetCount(&self->_actions);
    while (self->_actionStates._count < SlotCount)
    {
        ActionState Empty;
        Memory_Zero(&Empty, sizeof(Empty));
        GenericBuffer_AddLast(&self->_actionStates, &Empty);
    }
}

static void GrowCategoryStates(GameInput* self)
{
    size_t SlotCount = Registry_GetCount(&self->_categories);
    while (self->_categoryStates._count < SlotCount)
    {
        CategoryState Empty;
        Memory_Zero(&Empty, sizeof(Empty));
        GenericBuffer_AddLast(&self->_categoryStates, &Empty);
    }
}


// Static functions: kept entries.
static ControlsEntry* GetKeptEntries(const GameInput* self)
{
    return (ControlsEntry*)self->_keptEntries._data;
}

/* Finds a kept entry by its full key (they are in byte order): true with its index, or false with the index it would
 * have. */
static bool FindKeptEntry(const GameInput* self, const unsigned char* key, size_t* outIndex)
{
    size_t Low = 0U;
    size_t High = self->_keptEntries._count;
    const ControlsEntry* Entries = GetKeptEntries(self);
    while (Low < High)
    {
        size_t Middle = Low + ((High - Low) / 2U);
        int Order = CompareTexts(Entries[Middle].Key, key);
        if (Order == 0)
        {
            *outIndex = Middle;
            return true;
        }
        if (Order < 0)
        {
            Low = Middle + 1U;
        }
        else
        {
            High = Middle;
        }
    }
    *outIndex = Low;
    return false;
}

static void ClearKeptEntries(GameInput* self)
{
    ControlsEntry* Entries = GetKeptEntries(self);
    for (size_t Index = 0U; Index < self->_keptEntries._count; Index++)
    {
        Memory_Free(Entries[Index].Key);
    }
    GenericBuffer_Clear(&self->_keptEntries);
}

/* Gives a newly registered action the bindings of a kept entry for its key, as set by the player; true if there was
 * one (it is removed). */
static bool TakeKeptEntry(GameInput* self, GameActionId id)
{
    size_t Index = 0U;
    if (!FindKeptEntry(self, GameInput_GetActionKeyText(self, id), &Index))
    {
        return false;
    }
    ActionState* State = FindActionState(self, id);
    ControlsEntry* Entry = &GetKeptEntries(self)[Index];
    Memory_Copy(Entry->Bindings, State->Bindings, sizeof(State->Bindings));
    State->BindingCount = Entry->BindingCount;
    State->IsSetByPlayer = true;
    Memory_Free(Entry->Key);
    GenericBuffer_RemoveAt(&self->_keptEntries, Index);
    return true;
}


// Static functions: controls files.
static bool HasUtf8ByteOrderMark(const GenericBuffer* bytes)
{
    const unsigned char* Data = bytes->_data;
    return (bytes->_count >= UTF8_BOM_LENGTH) && (Data[0] == 0xEFU) && (Data[1] == 0xBBU) && (Data[2] == 0xBFU);
}

static void FreeControlsEntries(GenericBuffer* entries)
{
    ControlsEntry* Entries = (ControlsEntry*)entries->_data;
    for (size_t Index = 0U; Index < entries->_count; Index++)
    {
        Memory_Free(Entries[Index].Key);
    }
    Memory_Free(entries->_data);
}

static ComparisonResult CompareControlsEntries(GenericBuffer* buffer, GenericBufferElementData a,
    GenericBufferElementData b, const UserData* userData)
{
    UNUSED(buffer);
    UNUSED(userData);
    int Order = CompareTexts(((const ControlsEntry*)a._element)->Key, ((const ControlsEntry*)b._element)->Key);
    return COMPARE_NUMBER(Order, 0);
}

/* Reads one entry's binding list (a JSON array of binding strings) into @p outEntry; @p key is the entry's full key,
 * for messages. */
static Error ReadBindingList(const JSONObjectValue* listValue, const char* key, ControlsEntry* outEntry)
{
    if (listValue->Type != JSONValueType_Array)
    {
        return Error_Construct4(ErrorCode_InvalidJSON, "Controls file: \"%s\" must be a list of bindings.", key);
    }

    JSONArray* List = listValue->Value.Array;
    size_t Count = JSONArray_GetElementCount(List);
    if (Count > GAME_INPUT_MAX_BINDINGS_PER_ACTION)
    {
        return Error_Construct4(ErrorCode_InvalidJSON, "Controls file: \"%s\" has %" PRIu64 " bindings; at most %" PRIu64
            " are allowed.", key, (uint64_t)Count, (uint64_t)GAME_INPUT_MAX_BINDINGS_PER_ACTION);
    }

    for (size_t Index = 0; Index < Count; Index++)
    {
        JSONObjectValue Element;
        Error Result = JSONArray_Get(List, Index, &Element);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        if (Element.Type != JSONValueType_String)
        {
            return Error_Construct4(ErrorCode_InvalidJSON, "Controls file: binding %" PRIu64 " of \"%s\" must be a string.",
                (uint64_t)Index, key);
        }

        // WRJSON strings are not terminated: copy into a terminated buffer (binding texts are short).
        const GenericBuffer* Source = Element.Value.String;
        unsigned char Text[GAME_INPUT_MAX_BINDING_TEXT_BYTES + 1];
        if (Source->_count > GAME_INPUT_MAX_BINDING_TEXT_BYTES)
        {
            return Error_Construct4(ErrorCode_InvalidJSON, "Controls file: binding %" PRIu64 " of \"%s\" is too long.",
                (uint64_t)Index, key);
        }
        if (Source->_count > 0U)
        {
            Memory_Copy(Source->_data, Text, Source->_count);
        }
        Text[Source->_count] = 0;
        bool HasEmbeddedTerminator = false;
        for (size_t ByteIndex = 0; ByteIndex < Source->_count; ByteIndex++)
        {
            HasEmbeddedTerminator = HasEmbeddedTerminator || (Text[ByteIndex] == 0);
        }
        if (HasEmbeddedTerminator || !GameInputBinding_TryParse(Text, &outEntry->Bindings[Index]))
        {
            char Printable[GAME_INPUT_MAX_BINDING_TEXT_BYTES + 1];
            CopyPrintable(Text, Source->_count, Printable, sizeof(Printable));
            return Error_Construct4(ErrorCode_InvalidJSON, "Controls file: \"%s\" in \"%s\" is not a binding (modifiers "
                "then a key id, joined by '+'; see references/controls_structure.md).", Printable, key);
        }
    }

    outEntry->BindingCount = Count;
    return Error_CreateSuccess();
}

/* Reads one entry of "bindings": its key in full and its bindings, appended to @p entries (owned key). */
static Error ReadControlsEntry(const JSONCompoundEntry* entry, GenericBuffer* entries)
{
    size_t Length = StringUTF8_GetByteLength(entry->Key);
    unsigned char KeyStorage[NAMESPACED_KEY_BUFFER_SIZE];
    GenericBuffer FullKey;
    GenericBuffer_CreateConstant(&FullKey, KeyStorage, sizeof(KeyStorage), sizeof(unsigned char), 0);
    Error Result = NamespacedKey_Parse(entry->Key, Length, NAMESPACED_KEY_DEFAULT_NAMESPACE, &FullKey);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        char Printable[PRINTABLE_KEY_CAPACITY];
        CopyPrintable(entry->Key, Length, Printable, sizeof(Printable));
        const char* Problem = NamespacedKey_DescribeProblem(entry->Key, Length, NAMESPACED_KEY_DEFAULT_NAMESPACE);
        return Error_Construct4(ErrorCode_InvalidJSON, "Controls file: \"%s\" is not an action key: %s.", Printable,
            (Problem != NULL) ? Problem : "it could not be read");
    }

    ControlsEntry Entry;
    Memory_Zero(&Entry, sizeof(Entry));
    Result = ReadBindingList(&entry->Value, (const char*)KeyStorage, &Entry);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Entry.Key = DuplicateText(KeyStorage);
    GenericBuffer_AddLast(entries, &Entry);
    return Error_CreateSuccess();
}

/* Reads a parsed controls document into staged entries (full keys, in byte order), checking every rule. */
static Error ReadControlsDocument(const JSONObjectValue* root, GenericBuffer* entries)
{
    if (root->Type != JSONValueType_Compound)
    {
        return Error_Construct2(ErrorCode_InvalidJSON, "Controls file: the root must be a JSON object.");
    }

    JSONObjectValue BindingsValue;
    bool WasFound = false;
    Error Result = JSONCompound_GetOptional(root->Value.Compound, CONTROLS_KEY_BINDINGS, &BindingsValue, &WasFound);
    if ((Result.Code != ErrorCode_Success) || !WasFound)
    {
        return Result; // A file without "bindings" changes nothing.
    }
    if (BindingsValue.Type != JSONValueType_Compound)
    {
        return Error_Construct2(ErrorCode_InvalidJSON, "Controls file: \"bindings\" must be an object.");
    }
    JSONCompound* Bindings = BindingsValue.Value.Compound;
    size_t EntryCount = JSONCompound_GetEntryCount(Bindings);
    if (EntryCount > GAME_INPUT_MAX_CONTROLS_ENTRIES)
    {
        return Error_Construct4(ErrorCode_InvalidJSON, "Controls file: %" PRIu64 " entries; at most %" PRIu64
            " are allowed.", (uint64_t)EntryCount, (uint64_t)GAME_INPUT_MAX_CONTROLS_ENTRIES);
    }

    ICollection* Collection = JSONCompound_GetEntryCollection(Bindings);
    CollectionEnumerator* Enumerator = (Collection != NULL) ? ICollection_CreateEnumerator(Collection) : NULL;
    if (Enumerator == NULL)
    {
        return Error_Construct2(ErrorCode_BufferTooSmall, "Controls file: could not enumerate \"bindings\".");
    }
    bool HasNext = false;
    Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
    while ((Result.Code == ErrorCode_Success) && HasNext)
    {
        JSONCompoundEntry Entry;
        Result = CollectionEnumerator_NextByValue(Enumerator, &Entry);
        if (Result.Code == ErrorCode_Success)
        {
            Result = ReadControlsEntry(&Entry, entries);
        }
        if (Result.Code == ErrorCode_Success)
        {
            Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
        }
    }
    CollectionEnumerator_Destroy(Enumerator);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    // "move_forward" and "tbp:move_forward" are one key.
    GenericBuffer_SortAscendingAllocating(entries, CompareControlsEntries, NULL);
    const ControlsEntry* Staged = (const ControlsEntry*)entries->_data;
    for (size_t Index = 1U; Index < entries->_count; Index++)
    {
        if (CompareTexts(Staged[Index - 1U].Key, Staged[Index].Key) == 0)
        {
            return Error_Construct4(ErrorCode_InvalidJSON, "Controls file: \"%s\" is listed twice.",
                (const char*)Staged[Index].Key);
        }
    }
    return Error_CreateSuccess();
}

/* Applies one staged entry: to its action's slot (active or withdrawn), else as a kept entry. Takes the entry's key. */
static void ApplyControlsEntry(GameInput* self, ControlsEntry* entry)
{
    uint32_t KeyId = NAMESPACED_KEY_ID_NONE;
    GameActionId Id = 0U;
    if (KeyRegistry_TryFind(self->_keys, entry->Key, &KeyId) && Registry_FindSlot(&self->_actions, KeyId, &Id))
    {
        const ActionEntry* Action = GetActionEntry(self, Id);
        if ((entry->BindingCount == 0U) && (Action != NULL) && Action->KeepsABinding)
        {
            RestoreDefaults(self, Id);
            LogLine(self, LogLevel_Warning, "the controls file leaves \"%s\" without a binding, but it always keeps one: "
                "it keeps its defaults.", (const char*)entry->Key);
        }
        else
        {
            ActionState* State = FindActionState(self, Id);
            Memory_Copy(entry->Bindings, State->Bindings, sizeof(State->Bindings));
            State->BindingCount = entry->BindingCount;
            State->IsSetByPlayer = true;
        }
        Memory_Free(entry->Key);
        entry->Key = NULL;
        return;
    }

    size_t Index = 0U;
    if (FindKeptEntry(self, entry->Key, &Index))
    {
        ControlsEntry* Kept = &GetKeptEntries(self)[Index];
        Memory_Free(Kept->Key);
        *Kept = *entry;
    }
    else
    {
        GenericBuffer_Insert(&self->_keptEntries, entry, Index);
    }
    entry->Key = NULL;
}

/* Adds one key's bindings to @p bindings as a JSON array of binding strings. */
static Error WriteBindingList(const unsigned char* key, const GameInputBinding* bindings, size_t count,
    JSONObjectPool* pool, JSONCompound* outBindings)
{
    JSONArray* List = NULL;
    Error Result = JSONObjectPool_BorrowArray(pool, &List);
    for (size_t Index = 0; (Result.Code == ErrorCode_Success) && (Index < count); Index++)
    {
        unsigned char TextStorage[GAME_INPUT_MAX_BINDING_TEXT_BYTES + 1];
        GenericBuffer Text;
        GenericBuffer_CreateConstant(&Text, TextStorage, sizeof(TextStorage), sizeof(unsigned char), 0);
        Result = GameInputBinding_AppendText(&bindings[Index], &Text);

        GenericBuffer* String = NULL;
        if (Result.Code == ErrorCode_Success)
        {
            Result = JSONObjectPool_BorrowString(pool, &String);
        }
        // JSON strings hold no terminator.
        if ((Result.Code == ErrorCode_Success) && !GenericBuffer_AppendRangeBytes(String, TextStorage, Text._count - 1U))
        {
            Result = Error_Construct2(ErrorCode_BufferTooLarge, "GameInput: could not write a binding.");
        }
        if (Result.Code == ErrorCode_Success)
        {
            JSONObjectValue StringValue = JSONObjectValue_CreateString(String);
            Result = JSONArray_Add(List, &StringValue);
        }
    }
    if (Result.Code == ErrorCode_Success)
    {
        JSONObjectValue ListValue = JSONObjectValue_CreateArray(List);
        Result = JSONCompound_Set(outBindings, key, &ListValue);
    }
    return Result; // Anything borrowed and not attached is reclaimed when the caller deconstructs the pool.
}

/* Builds the controls document (what the player set, in byte order of the keys) and serializes it into @p outText. */
static Error WriteControlsDocument(const GameInput* self, JSONObjectPool* pool, GenericBuffer* outText)
{
    // The entries to write, borrowing their keys: the player's actions and the kept entries.
    GenericBuffer Written;
    GenericBuffer_AllocateVariable(&Written, INITIAL_ACTION_CAPACITY, sizeof(ControlsEntry));
    const ActionState* States = GetActionStates(self);
    for (GameActionId Id = 0U; Id < self->_actionStates._count; Id++)
    {
        if (States[Id].IsSetByPlayer)
        {
            ControlsEntry Entry = { .Key = (unsigned char*)GameInput_GetActionKeyText(self, Id),
                .BindingCount = States[Id].BindingCount };
            Memory_Copy(States[Id].Bindings, Entry.Bindings, sizeof(Entry.Bindings));
            GenericBuffer_AddLast(&Written, &Entry);
        }
    }
    GenericBuffer_AddLastRange(&Written, self->_keptEntries._data, self->_keptEntries._count);
    GenericBuffer_SortAscendingAllocating(&Written, CompareControlsEntries, NULL);

    JSONCompound* Root = NULL;
    JSONCompound* Bindings = NULL;
    Error Result = JSONObjectPool_BorrowCompound(pool, &Root);
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONObjectPool_BorrowCompound(pool, &Bindings);
    }
    const ControlsEntry* Entries = (const ControlsEntry*)Written._data;
    for (size_t Index = 0U; (Result.Code == ErrorCode_Success) && (Index < Written._count); Index++)
    {
        Result = WriteBindingList(Entries[Index].Key, Entries[Index].Bindings, Entries[Index].BindingCount, pool,
            Bindings);
    }
    Memory_Free(Written._data);
    if (Result.Code == ErrorCode_Success)
    {
        JSONObjectValue BindingsValue = JSONObjectValue_CreateCompound(Bindings);
        Result = JSONCompound_Set(Root, CONTROLS_KEY_BINDINGS, &BindingsValue);
    }
    if (Result.Code == ErrorCode_Success)
    {
        JSONObjectValue RootValue = JSONObjectValue_CreateCompound(Root);
        Result = JSON_Serialize(&RootValue, outText,
            (JSONSerializeFlags)(JSONSerializeFlags_PrettyPrinted | JSONSerializeFlags_NullTerminated));
    }
    return Result;
}

/* Releases a JSON pool, keeping the first error. */
static Error ReleasePool(JSONObjectPool* pool, Error result)
{
    if (pool == NULL)
    {
        return result;
    }
    Error PoolResult = JSONObjectPool_Deconstruct(pool);
    if ((result.Code == ErrorCode_Success) && (PoolResult.Code != ErrorCode_Success))
    {
        return PoolResult;
    }
    Error_Deconstruct(&PoolResult);
    return result;
}


// Static functions: the listing.
/* Orders category ids as the controls menu lists them: by effective order, then by key. */
static ComparisonResult CompareCategoriesForMenu(GenericBuffer* buffer, GenericBufferElementData a,
    GenericBufferElementData b, const UserData* userData)
{
    UNUSED(buffer);
    const GameInput* Self = UserData_GetPointer(userData);
    GameActionCategoryId First = *(const GameActionCategoryId*)a._element;
    GameActionCategoryId Second = *(const GameActionCategoryId*)b._element;
    GameActionCategoryInfo FirstInfo;
    GameActionCategoryInfo SecondInfo;
    bool AreActive = GameInput_TryGetCategoryInfo(Self, First, &FirstInfo)
        && GameInput_TryGetCategoryInfo(Self, Second, &SecondInfo);
    if (AreActive && (FirstInfo.Order != SecondInfo.Order))
    {
        return COMPARE_NUMBER(FirstInfo.Order, SecondInfo.Order);
    }
    int Order = CompareTexts(GameInput_GetCategoryKeyText(Self, First), GameInput_GetCategoryKeyText(Self, Second));
    return COMPARE_NUMBER(Order, 0);
}

/* Logs one action's line: key, display name, bindings, who set them, its source and its override's. */
static void LogActionLine(const GameInput* self, GameActionId id)
{
    GameActionInfo Info;
    if (!GameInput_TryGetActionInfo(self, id, &Info))
    {
        return;
    }
    const ActionState* State = FindActionState(self, id);
    unsigned char TextStorage[(GAME_INPUT_MAX_BINDING_TEXT_BYTES + 2U) * GAME_INPUT_MAX_BINDINGS_PER_ACTION + 1U];
    GenericBuffer Text;
    GenericBuffer_CreateConstant(&Text, TextStorage, sizeof(TextStorage), sizeof(unsigned char), 0);
    GenericBuffer_NullTerminate(&Text);
    for (size_t Index = 0U; Index < State->BindingCount; Index++)
    {
        if (Index > 0U)
        {
            GenericBuffer_SetCount(&Text, Text._count - 1U);
            GenericBuffer_AppendString(&Text, (const unsigned char*)u8", ");
            GenericBuffer_NullTerminate(&Text);
        }
        Error TextResult = GameInputBinding_AppendText(&State->Bindings[Index], &Text);
        Error_Deconstruct(&TextResult);
    }

    char SourceText[PRINTABLE_KEY_CAPACITY] = "";
    uint32_t Source = GameInput_GetActionSource(self, id);
    if (Source != ENGINE_SOURCE)
    {
        snprintf(SourceText, sizeof(SourceText), ", from %s", DescribeSource(self, Source));
    }
    char OverrideText[PRINTABLE_KEY_CAPACITY] = "";
    uint32_t OverrideSource = ENGINE_SOURCE;
    if (GameInput_TryGetActionOverrideSource(self, id, &OverrideSource))
    {
        snprintf(OverrideText, sizeof(OverrideText), ", changed by %s", DescribeSource(self, OverrideSource));
    }
    LogLine(self, LogLevel_Info, "  %s \"%s\": %s%s%s%s", (const char*)GameInput_GetActionKeyText(self, id),
        (const char*)Info.DisplayName, (State->BindingCount > 0U) ? (const char*)TextStorage : "unbound",
        State->IsSetByPlayer ? " (set by the player)" : "", SourceText, OverrideText);
}

/* Logs the active actions of a category (any category that is not active when @p isUncategorized), by key. */
static void LogCategoryActions(const GameInput* self, GameActionCategoryId category, bool isUncategorized,
    GenericBuffer* scratch)
{
    GenericBuffer_Clear(scratch);
    Error FindResult = GameInput_FindActionsByPrefix(self, (const unsigned char*)u8"", scratch);
    Error_Deconstruct(&FindResult);
    const GameActionId* Ids = (const GameActionId*)scratch->_data;
    for (size_t Index = 0U; Index < scratch->_count; Index++)
    {
        const ActionEntry* Entry = GetActionEntry(self, Ids[Index]);
        bool IsListed = isUncategorized ? !GameInput_IsCategoryActive(self, Entry->Category)
            : (Entry->Category == category);
        if (IsListed)
        {
            LogActionLine(self, Ids[Index]);
        }
    }
}


// Static functions: construction.
static Error ConstructRegistry(GameInput* self, Registry* registry, const unsigned char* key, size_t elementSize,
    uint32_t maxEntryCount, RegistryElementDeconstructor deconstructElement)
{
    uint32_t KeyId = NAMESPACED_KEY_ID_NONE;
    Error Result = KeyRegistry_Intern(self->_keys, key, &KeyId);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    RegistryConstructOptions Options = RegistryConstructOptions_CreateDefault(self->_keys, KeyId, elementSize,
        maxEntryCount);
    Options.DeconstructElement = deconstructElement;
    return Registry_Construct(registry, &Options);
}

/* Registers the engine's categories and actions in their enums' order, checking each gets its enum value as its id. */
static Error RegisterBuiltIns(GameInput* self)
{
    for (uint32_t Index = 0U; Index < (uint32_t)GameActionCategory_Count; Index++)
    {
        const BuiltInCategory* BuiltIn = &BuiltInCategories[Index];
        GameActionCategoryInfo Info = { .DisplayName = BuiltIn->DisplayName, .Description = BuiltIn->Description,
            .Order = BuiltIn->Order };
        uint32_t KeyId = NAMESPACED_KEY_ID_NONE;
        GameActionCategoryId Id = 0U;
        Error Result = KeyRegistry_Intern(self->_keys, BuiltIn->Key, &KeyId);
        if (Result.Code == ErrorCode_Success)
        {
            Result = GameInput_RegisterCategory(self, KeyId, &Info, ENGINE_SOURCE, &Id);
        }
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        if (Id != Index)
        {
            return Error_Construct4(ErrorCode_InvalidState, "GameInput_Construct: the engine's category \"%s\" got id %"
                PRIu32 ", not its enum value %" PRIu32 ".", (const char*)BuiltIn->Key, Id, Index);
        }
    }
    for (uint32_t Index = 0U; Index < (uint32_t)GameAction_Count; Index++)
    {
        const BuiltInAction* BuiltIn = &BuiltInActions[Index];
        GameActionInfo Info = { .DisplayName = BuiltIn->DisplayName, .Description = BuiltIn->Description,
            .Category = BuiltIn->Category, .DefaultBindingCount = BuiltIn->DefaultCount, .Kind = GameActionKind_Button,
            .KeepsABinding = BuiltIn->KeepsABinding };
        Memory_Copy(BuiltIn->Defaults, Info.DefaultBindings, sizeof(Info.DefaultBindings));
        uint32_t KeyId = NAMESPACED_KEY_ID_NONE;
        GameActionId Id = 0U;
        Error Result = KeyRegistry_Intern(self->_keys, BuiltIn->Key, &KeyId);
        if (Result.Code == ErrorCode_Success)
        {
            Result = GameInput_RegisterAction(self, KeyId, &Info, ENGINE_SOURCE, &Id);
        }
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        if (Id != Index)
        {
            return Error_Construct4(ErrorCode_InvalidState, "GameInput_Construct: the engine's action \"%s\" got id %"
                PRIu32 ", not its enum value %" PRIu32 ".", (const char*)BuiltIn->Key, Id, Index);
        }
    }
    return Error_CreateSuccess();
}


// Public functions: lifecycle.
Error GameInput_Construct(GameInput* self, KeyRegistry* keys, RegistryRoot* root, Logger* logger)
{
    if ((self == NULL) || (keys == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "GameInput_Construct: self and keys must not be NULL.");
    }
    Memory_Zero(self, sizeof(*self));
    GameInputSample_Clear(&self->_sample);
    self->_keys = keys;
    self->_logger = logger;
    GenericBuffer_AllocateVariable(&self->_actionStates, INITIAL_ACTION_CAPACITY, sizeof(ActionState));
    GenericBuffer_AllocateVariable(&self->_categoryStates, GameActionCategory_Count, sizeof(CategoryState));
    GenericBuffer_AllocateVariable(&self->_keyIndex, INITIAL_ACTION_CAPACITY, sizeof(KeyIndexEntry));
    GenericBuffer_AllocateVariable(&self->_keptEntries, 0U, sizeof(ControlsEntry));

    bool AreActionsBuilt = false;
    bool AreCategoriesBuilt = false;
    Error Result = ConstructRegistry(self, &self->_actions, GAME_INPUT_ACTION_REGISTRY_KEY, sizeof(ActionEntry),
        GAME_INPUT_MAX_ACTIONS, DeconstructActionEntry);
    AreActionsBuilt = (Result.Code == ErrorCode_Success);
    if (Result.Code == ErrorCode_Success)
    {
        Result = ConstructRegistry(self, &self->_categories, GAME_INPUT_CATEGORY_REGISTRY_KEY, sizeof(CategoryEntry),
            GAME_INPUT_MAX_CATEGORIES, DeconstructCategoryEntry);
        AreCategoriesBuilt = (Result.Code == ErrorCode_Success);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = RegisterBuiltIns(self);
    }
    if ((Result.Code == ErrorCode_Success) && (root != NULL))
    {
        Result = RegistryRoot_Add(root, &self->_actions);
        if (Result.Code == ErrorCode_Success)
        {
            Result = RegistryRoot_Add(root, &self->_categories);
            if (Result.Code != ErrorCode_Success)
            {
                Error WithdrawResult = RegistryRoot_Withdraw(root, &self->_actions);
                Error_Deconstruct(&WithdrawResult);
            }
        }
        self->_root = (Result.Code == ErrorCode_Success) ? root : NULL;
    }
    if (Result.Code == ErrorCode_Success)
    {
        return Result;
    }

    // Undo what was built; the struct ends zeroed.
    if (AreActionsBuilt)
    {
        Registry_Deconstruct(&self->_actions);
    }
    if (AreCategoriesBuilt)
    {
        Registry_Deconstruct(&self->_categories);
    }
    Memory_Free(self->_actionStates._data);
    Memory_Free(self->_categoryStates._data);
    Memory_Free(self->_keyIndex._data);
    Memory_Free(self->_keptEntries._data);
    Memory_Zero(self, sizeof(*self));
    return Result;
}

Error GameInput_Deconstruct(GameInput* self)
{
    if ((self == NULL) || (self->_keys == NULL))
    {
        return Error_CreateSuccess(); // Not constructed: nothing to release.
    }
    if (self->_root != NULL)
    {
        Error ActionsResult = RegistryRoot_Withdraw(self->_root, &self->_actions);
        Error_Deconstruct(&ActionsResult);
        Error CategoriesResult = RegistryRoot_Withdraw(self->_root, &self->_categories);
        Error_Deconstruct(&CategoriesResult);
    }
    ActionState* Actions = GetActionStates(self);
    for (size_t Index = 0U; Index < self->_actionStates._count; Index++)
    {
        FreeActionOverride(&Actions[Index].Override);
    }
    CategoryState* Categories = GetCategoryStates(self);
    for (size_t Index = 0U; Index < self->_categoryStates._count; Index++)
    {
        FreeCategoryOverride(&Categories[Index].Override);
    }
    ClearKeptEntries(self);
    Registry_Deconstruct(&self->_actions);
    Registry_Deconstruct(&self->_categories);
    Memory_Free(self->_actionStates._data);
    Memory_Free(self->_categoryStates._data);
    Memory_Free(self->_keyIndex._data);
    Memory_Free(self->_keptEntries._data);
    Memory_Zero(self, sizeof(*self));
    return Error_CreateSuccess();
}


// Public functions: ticks.
void GameInputSample_Clear(GameInputSample* self)
{
    Memory_Zero(self, sizeof(*self));
}
void GameInput_SampleDevices(GameInputSample* outSample)
{
    GameInputSample_Clear(outSample);
    for (int32_t KeyIndex = (int32_t)GameKey_Unknown + 1; KeyIndex < (int32_t)GameKey_Count; KeyIndex++)
    {
        outSample->KeyStates[KeyIndex] = SampleKey((GameKey)KeyIndex);
    }

    outSample->MousePosition = GetMousePosition();
    outSample->MouseDelta = GetMouseDelta();
    outSample->WheelDelta = GetMouseWheelMoveV();
    outSample->WindowSize = (Vector2){ .x = (float)GetScreenWidth(), .y = (float)GetScreenHeight() };
    outSample->IsCursorCaptured = IsCursorHidden();

    // Drain the whole queue even when it holds more than fits, so nothing is left for a later reader.
    int Codepoint = GetCharPressed();
    while (Codepoint != 0)
    {
        if ((outSample->TextCodepointCount < GAME_INPUT_MAX_TEXT_CODEPOINTS) && CharUTF8_IsCodePointValid((CodePoint)Codepoint))
        {
            outSample->TextCodepoints[outSample->TextCodepointCount] = (uint32_t)Codepoint;
            outSample->TextCodepointCount++;
        }
        Codepoint = GetCharPressed();
    }
}

void GameInput_BeginTick(GameInput* self, const GameInputSample* sample)
{
    self->_sample = *sample;
    if (self->_sample.TextCodepointCount > GAME_INPUT_MAX_TEXT_CODEPOINTS)
    {
        self->_sample.TextCodepointCount = GAME_INPUT_MAX_TEXT_CODEPOINTS;
    }

    // A consumed key stays consumed while held and on the tick of its release; a new press starts unconsumed.
    for (int32_t KeyIndex = (int32_t)GameKey_Unknown + 1; KeyIndex < (int32_t)GameKey_Count; KeyIndex++)
    {
        uint8_t State = self->_sample.KeyStates[KeyIndex];
        bool IsNewPress = (State & GAME_INPUT_KEY_STATE_PRESSED) != 0;
        bool IsStillInvolved = (State & (GAME_INPUT_KEY_STATE_DOWN | GAME_INPUT_KEY_STATE_RELEASED)) != 0;
        if (IsNewPress || !IsStillInvolved)
        {
            self->_isKeyConsumed[KeyIndex] = false;
        }
    }
    self->_isWheelConsumed = false;
    self->_isTextConsumed = false;
    self->_isPointerConsumed = false;
}

void GameInput_Poll(GameInput* self)
{
    GameInputSample Sample;
    GameInput_SampleDevices(&Sample);
    GameInput_BeginTick(self, &Sample);
}


// Public functions: keys.
uint8_t GameInput_GetRawKeyState(const GameInput* self, GameKey key)
{
    return GameKey_IsValid(key) ? self->_sample.KeyStates[key] : 0;
}

uint8_t GameInput_GetKeyState(const GameInput* self, GameKey key)
{
    if (!GameKey_IsValid(key) || self->_isKeyConsumed[key])
    {
        return 0;
    }
    return self->_sample.KeyStates[key];
}

bool GameInput_IsKeyDown(const GameInput* self, GameKey key)
{
    return (GameInput_GetKeyState(self, key) & GAME_INPUT_KEY_STATE_DOWN) != 0;
}

bool GameInput_IsKeyPressed(const GameInput* self, GameKey key)
{
    return (GameInput_GetKeyState(self, key) & GAME_INPUT_KEY_STATE_PRESSED) != 0;
}

bool GameInput_IsKeyReleased(const GameInput* self, GameKey key)
{
    return (GameInput_GetKeyState(self, key) & GAME_INPUT_KEY_STATE_RELEASED) != 0;
}

bool GameInput_IsKeyPressedOrRepeated(const GameInput* self, GameKey key)
{
    return (GameInput_GetKeyState(self, key) & (GAME_INPUT_KEY_STATE_PRESSED | GAME_INPUT_KEY_STATE_REPEATED)) != 0;
}

bool GameInput_IsKeyConsumed(const GameInput* self, GameKey key)
{
    return GameKey_IsValid(key) && self->_isKeyConsumed[key];
}

void GameInput_ConsumeKey(GameInput* self, GameKey key)
{
    if (GameKey_IsValid(key))
    {
        self->_isKeyConsumed[key] = true;
    }
}

uint8_t GameInput_GetHeldModifiers(const GameInput* self)
{
    uint8_t Modifiers = GAME_KEY_MODIFIER_NONE;
    for (int32_t KeyIndex = (int32_t)GameKey_LeftShift; KeyIndex <= (int32_t)GameKey_RightSuper; KeyIndex++)
    {
        if ((self->_sample.KeyStates[KeyIndex] & GAME_INPUT_KEY_STATE_DOWN) != 0)
        {
            Modifiers |= GameKey_GetModifierFlag((GameKey)KeyIndex);
        }
    }
    return Modifiers;
}


// Public functions: mouse, wheel and pointer.
Vector2 GameInput_GetMousePosition(const GameInput* self)
{
    return self->_sample.MousePosition;
}

Vector2 GameInput_GetMouseDelta(const GameInput* self)
{
    return self->_sample.MouseDelta;
}

Vector2 GameInput_GetWindowSize(const GameInput* self)
{
    return self->_sample.WindowSize;
}

bool GameInput_IsCursorCaptured(const GameInput* self)
{
    return self->_sample.IsCursorCaptured;
}

Vector2 GameInput_GetWheelDelta(const GameInput* self)
{
    return self->_isWheelConsumed ? (Vector2){ .x = 0.0f, .y = 0.0f } : self->_sample.WheelDelta;
}

void GameInput_ConsumeWheel(GameInput* self)
{
    self->_isWheelConsumed = true;
}

bool GameInput_IsPointerConsumed(const GameInput* self)
{
    return self->_isPointerConsumed;
}

void GameInput_ConsumePointer(GameInput* self)
{
    self->_isPointerConsumed = true;
}


// Public functions: text.
size_t GameInput_GetTextCodepointCount(const GameInput* self)
{
    return self->_isTextConsumed ? 0U : self->_sample.TextCodepointCount;
}

uint32_t GameInput_GetTextCodepoint(const GameInput* self, size_t index)
{
    return (index < GameInput_GetTextCodepointCount(self)) ? self->_sample.TextCodepoints[index] : 0U;
}

void GameInput_ConsumeText(GameInput* self)
{
    self->_isTextConsumed = true;
}




// Public functions: registration.
Error GameInput_RegisterCategory(GameInput* self, uint32_t keyId, const GameActionCategoryInfo* info,
    uint32_t sourceKeyId, GameActionCategoryId* outId)
{
    static const char* const Function = "GameInput_RegisterCategory";
    if ((self == NULL) || (info == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "GameInput_RegisterCategory: self and info must not be NULL.");
    }
    Error Result = CheckDisplayName(Function, info->DisplayName);
    if (Result.Code == ErrorCode_Success)
    {
        Result = CheckDescription(Function, info->Description);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    CategoryEntry Entry = { .DisplayName = DuplicateText(info->DisplayName),
        .Description = DuplicateText(info->Description), .Order = info->Order };
    GameActionCategoryId Id = 0U;
    Result = Registry_Register(&self->_categories, keyId, &Entry, sourceKeyId, &Id);
    if (Result.Code != ErrorCode_Success)
    {
        DeconstructCategoryEntry(&Entry, NULL);
        return Result;
    }
    GrowCategoryStates(self);
    if (outId != NULL)
    {
        *outId = Id;
    }
    return Error_CreateSuccess();
}

Error GameInput_RegisterAction(GameInput* self, uint32_t keyId, const GameActionInfo* info, uint32_t sourceKeyId,
    GameActionId* outId)
{
    static const char* const Function = "GameInput_RegisterAction";
    if ((self == NULL) || (info == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "GameInput_RegisterAction: self and info must not be NULL.");
    }
    Error Result = CheckDisplayName(Function, info->DisplayName);
    if (Result.Code == ErrorCode_Success)
    {
        Result = CheckDescription(Function, info->Description);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = CheckDefaults(Function, info->DefaultBindings, info->DefaultBindingCount);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (((int32_t)info->Kind < 0) || (info->Kind >= GameActionKind_Count))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "GameInput_RegisterAction: the kind is not a GameActionKind.");
    }
    if (info->KeepsABinding && (info->DefaultBindingCount == 0U))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "GameInput_RegisterAction: an action that always keeps a binding needs a default binding.");
    }
    if (!Registry_IsActive(&self->_categories, info->Category))
    {
        return Error_Construct4(ErrorCode_InvalidOperation, "GameInput_RegisterAction: category %" PRIu32
            " is not registered.", info->Category);
    }

    ActionEntry Entry = { .DisplayName = DuplicateText(info->DisplayName),
        .Description = DuplicateText(info->Description), .Category = info->Category,
        .DefaultBindingCount = info->DefaultBindingCount, .Kind = info->Kind, .KeepsABinding = info->KeepsABinding };
    Memory_Copy(info->DefaultBindings, Entry.DefaultBindings, sizeof(Entry.DefaultBindings));
    bool IsNew = false;
    GameActionId Id = 0U;
    if (!Registry_FindSlot(&self->_actions, keyId, &Id))
    {
        IsNew = true;
    }
    Result = Registry_Register(&self->_actions, keyId, &Entry, sourceKeyId, &Id);
    if (Result.Code != ErrorCode_Success)
    {
        DeconstructActionEntry(&Entry, NULL);
        return Result;
    }
    GrowActionStates(self);

    // A new action takes the player's bindings from a kept entry; a revived one keeps the player's; the rest follow
    // their defaults.
    ActionState* State = FindActionState(self, Id);
    bool IsTaken = IsNew && TakeKeptEntry(self, Id);
    if (!IsTaken && !State->IsSetByPlayer)
    {
        RestoreDefaults(self, Id);
    }
    if (State->IsSetByPlayer && (State->BindingCount == 0U) && info->KeepsABinding)
    {
        RestoreDefaults(self, Id);
        LogLine(self, LogLevel_Warning, "\"%s\" had no binding, but it always keeps one: it keeps its defaults.",
            (const char*)GameInput_GetActionKeyText(self, Id));
    }
    RebuildKeyIndex(self);
    if (sourceKeyId != ENGINE_SOURCE)
    {
        LogDefaultConflicts(self, Id);
    }
    if (outId != NULL)
    {
        *outId = Id;
    }
    return Error_CreateSuccess();
}

Error GameInput_OverrideAction(GameInput* self, GameActionId id, const GameActionOverride* override,
    uint32_t sourceKeyId)
{
    static const char* const Function = "GameInput_OverrideAction";
    if ((self == NULL) || (override == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "GameInput_OverrideAction: self and override must not be NULL.");
    }
    if (!override->OverridesDisplayName && !override->OverridesDescription && !override->OverridesDefaultBindings)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "GameInput_OverrideAction: the override changes nothing.");
    }
    Error Result = override->OverridesDisplayName ? CheckDisplayName(Function, override->DisplayName)
        : Error_CreateSuccess();
    if ((Result.Code == ErrorCode_Success) && override->OverridesDescription)
    {
        Result = CheckDescription(Function, override->Description);
    }
    if ((Result.Code == ErrorCode_Success) && override->OverridesDefaultBindings)
    {
        Result = CheckDefaults(Function, override->DefaultBindings, override->DefaultBindingCount);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!IsSourceValid(self, sourceKeyId))
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange, "GameInput_OverrideAction: source %" PRIu32
            " is not a key of the key registry.", sourceKeyId);
    }
    const ActionEntry* Entry = GetActionEntry(self, id);
    if (Entry == NULL)
    {
        return Error_Construct4(ErrorCode_InvalidOperation, "GameInput_OverrideAction: action %" PRIu32
            " is not registered.", id);
    }
    const char* Key = (const char*)GameInput_GetActionKeyText(self, id);
    if (override->OverridesDefaultBindings && (override->DefaultBindingCount == 0U) && Entry->KeepsABinding)
    {
        return Error_Construct4(ErrorCode_InvalidOperation, "GameInput_OverrideAction: \"%s\" always keeps a binding, "
            "so its defaults cannot be empty.", Key);
    }
    ActionState* State = FindActionState(self, id);
    if (State->HasOverride && (State->Override.Source != sourceKeyId))
    {
        return Error_Construct4(ErrorCode_InvalidOperation, "GameInput_OverrideAction: %s already changes \"%s\"; %s "
            "cannot as well.", DescribeSource(self, State->Override.Source), Key, DescribeSource(self, sourceKeyId));
    }

    FreeActionOverride(&State->Override);
    State->HasOverride = true;
    State->Override = (ActionOverride){ .Source = sourceKeyId,
        .OverridesDisplayName = override->OverridesDisplayName,
        .DisplayName = override->OverridesDisplayName ? DuplicateText(override->DisplayName) : NULL,
        .OverridesDescription = override->OverridesDescription,
        .Description = override->OverridesDescription ? DuplicateText(override->Description) : NULL,
        .OverridesDefaultBindings = override->OverridesDefaultBindings,
        .DefaultBindingCount = override->OverridesDefaultBindings ? override->DefaultBindingCount : 0U };
    if (override->OverridesDefaultBindings)
    {
        Memory_Copy(override->DefaultBindings, State->Override.DefaultBindings, sizeof(State->Override.DefaultBindings));
    }
    if (!State->IsSetByPlayer)
    {
        RestoreDefaults(self, id);
    }
    RebuildKeyIndex(self);
    if (override->OverridesDefaultBindings && (sourceKeyId != ENGINE_SOURCE))
    {
        LogDefaultConflicts(self, id);
    }
    return Error_CreateSuccess();
}

Error GameInput_OverrideCategory(GameInput* self, GameActionCategoryId id, const GameActionCategoryOverride* override,
    uint32_t sourceKeyId)
{
    static const char* const Function = "GameInput_OverrideCategory";
    if ((self == NULL) || (override == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "GameInput_OverrideCategory: self and override must not be NULL.");
    }
    if (!override->OverridesDisplayName && !override->OverridesDescription && !override->OverridesOrder)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "GameInput_OverrideCategory: the override changes nothing.");
    }
    Error Result = override->OverridesDisplayName ? CheckDisplayName(Function, override->DisplayName)
        : Error_CreateSuccess();
    if ((Result.Code == ErrorCode_Success) && override->OverridesDescription)
    {
        Result = CheckDescription(Function, override->Description);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!IsSourceValid(self, sourceKeyId))
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange, "GameInput_OverrideCategory: source %" PRIu32
            " is not a key of the key registry.", sourceKeyId);
    }
    if (!Registry_IsActive(&self->_categories, id))
    {
        return Error_Construct4(ErrorCode_InvalidOperation, "GameInput_OverrideCategory: category %" PRIu32
            " is not registered.", id);
    }
    CategoryState* State = &GetCategoryStates(self)[id];
    if (State->HasOverride && (State->Override.Source != sourceKeyId))
    {
        return Error_Construct4(ErrorCode_InvalidOperation, "GameInput_OverrideCategory: %s already changes \"%s\"; %s "
            "cannot as well.", DescribeSource(self, State->Override.Source),
            (const char*)GameInput_GetCategoryKeyText(self, id), DescribeSource(self, sourceKeyId));
    }

    FreeCategoryOverride(&State->Override);
    State->HasOverride = true;
    State->Override = (CategoryOverride){ .Source = sourceKeyId,
        .OverridesDisplayName = override->OverridesDisplayName,
        .DisplayName = override->OverridesDisplayName ? DuplicateText(override->DisplayName) : NULL,
        .OverridesDescription = override->OverridesDescription,
        .Description = override->OverridesDescription ? DuplicateText(override->Description) : NULL,
        .OverridesOrder = override->OverridesOrder, .Order = override->OverridesOrder ? override->Order : 0 };
    return Error_CreateSuccess();
}

uint32_t GameInput_WithdrawSource(GameInput* self, uint32_t sourceKeyId)
{
    if (self == NULL)
    {
        return 0U;
    }
    uint32_t Count = 0U;
    ActionState* Actions = GetActionStates(self);
    for (size_t Index = 0U; Index < self->_actionStates._count; Index++)
    {
        if (Actions[Index].HasOverride && (Actions[Index].Override.Source == sourceKeyId))
        {
            FreeActionOverride(&Actions[Index].Override);
            Actions[Index].HasOverride = false;
            Count++;
        }
    }
    CategoryState* Categories = GetCategoryStates(self);
    for (size_t Index = 0U; Index < self->_categoryStates._count; Index++)
    {
        if (Categories[Index].HasOverride && (Categories[Index].Override.Source == sourceKeyId))
        {
            FreeCategoryOverride(&Categories[Index].Override);
            Categories[Index].HasOverride = false;
            Count++;
        }
    }
    Count += Registry_WithdrawSource(&self->_actions, sourceKeyId);
    Count += Registry_WithdrawSource(&self->_categories, sourceKeyId);
    RefreshDefaults(self);
    RebuildKeyIndex(self);
    return Count;
}


// Public functions: actions and categories.
bool GameInput_FindAction(GameInput* self, uint32_t keyId, GameActionId* outId)
{
    return (self != NULL) && Registry_Find(&self->_actions, keyId, outId);
}

bool GameInput_FindActionText(GameInput* self, const unsigned char* key, GameActionId* outId)
{
    return (self != NULL) && Registry_FindText(&self->_actions, key, outId);
}

Error GameInput_FindActionsByPrefix(const GameInput* self, const unsigned char* prefix, GenericBuffer* outIds)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "GameInput_FindActionsByPrefix: self must not be NULL.");
    }
    return Registry_FindByPrefix(&self->_actions, prefix, outIds);
}

uint32_t GameInput_GetActionCount(const GameInput* self)
{
    return Registry_GetCount(&self->_actions);
}

bool GameInput_IsActionActive(const GameInput* self, GameActionId id)
{
    return Registry_IsActive(&self->_actions, id);
}

bool GameInput_TryGetActionInfo(const GameInput* self, GameActionId id, GameActionInfo* outInfo)
{
    const ActionEntry* Entry = GetActionEntry(self, id);
    if (Entry == NULL)
    {
        return false;
    }
    GameActionInfo Info = { .DisplayName = Entry->DisplayName, .Description = Entry->Description,
        .Category = Entry->Category, .DefaultBindingCount = Entry->DefaultBindingCount, .Kind = Entry->Kind,
        .KeepsABinding = Entry->KeepsABinding };
    Memory_Copy(Entry->DefaultBindings, Info.DefaultBindings, sizeof(Info.DefaultBindings));
    const ActionState* State = FindActionState(self, id);
    if (State->HasOverride)
    {
        const ActionOverride* Override = &State->Override;
        Info.DisplayName = Override->OverridesDisplayName ? Override->DisplayName : Info.DisplayName;
        Info.Description = Override->OverridesDescription ? Override->Description : Info.Description;
        if (IsOverridingDefaults(Entry, State))
        {
            Memory_Copy(Override->DefaultBindings, Info.DefaultBindings, sizeof(Info.DefaultBindings));
            Info.DefaultBindingCount = Override->DefaultBindingCount;
        }
    }
    *outInfo = Info;
    return true;
}

uint32_t GameInput_GetActionKey(const GameInput* self, GameActionId id)
{
    return Registry_GetKey(&self->_actions, id);
}

const unsigned char* GameInput_GetActionKeyText(const GameInput* self, GameActionId id)
{
    return Registry_GetKeyText(&self->_actions, id);
}

uint32_t GameInput_GetActionSource(const GameInput* self, GameActionId id)
{
    return Registry_GetSource(&self->_actions, id);
}

bool GameInput_TryGetActionOverrideSource(const GameInput* self, GameActionId id, uint32_t* outSourceKeyId)
{
    const ActionState* State = FindActionState(self, id);
    if ((State == NULL) || !State->HasOverride)
    {
        return false;
    }
    *outSourceKeyId = State->Override.Source;
    return true;
}

bool GameInput_FindCategory(GameInput* self, uint32_t keyId, GameActionCategoryId* outId)
{
    return (self != NULL) && Registry_Find(&self->_categories, keyId, outId);
}

bool GameInput_FindCategoryText(GameInput* self, const unsigned char* key, GameActionCategoryId* outId)
{
    return (self != NULL) && Registry_FindText(&self->_categories, key, outId);
}

Error GameInput_FindCategoriesByPrefix(const GameInput* self, const unsigned char* prefix, GenericBuffer* outIds)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "GameInput_FindCategoriesByPrefix: self must not be NULL.");
    }
    return Registry_FindByPrefix(&self->_categories, prefix, outIds);
}

uint32_t GameInput_GetCategoryCount(const GameInput* self)
{
    return Registry_GetCount(&self->_categories);
}

bool GameInput_IsCategoryActive(const GameInput* self, GameActionCategoryId id)
{
    return Registry_IsActive(&self->_categories, id);
}

bool GameInput_TryGetCategoryInfo(const GameInput* self, GameActionCategoryId id, GameActionCategoryInfo* outInfo)
{
    const CategoryEntry* Entry = GetCategoryEntry(self, id);
    if (Entry == NULL)
    {
        return false;
    }
    GameActionCategoryInfo Info = { .DisplayName = Entry->DisplayName, .Description = Entry->Description,
        .Order = Entry->Order };
    const CategoryState* State = &GetCategoryStates(self)[id];
    if (State->HasOverride)
    {
        const CategoryOverride* Override = &State->Override;
        Info.DisplayName = Override->OverridesDisplayName ? Override->DisplayName : Info.DisplayName;
        Info.Description = Override->OverridesDescription ? Override->Description : Info.Description;
        Info.Order = Override->OverridesOrder ? Override->Order : Info.Order;
    }
    *outInfo = Info;
    return true;
}

uint32_t GameInput_GetCategoryKey(const GameInput* self, GameActionCategoryId id)
{
    return Registry_GetKey(&self->_categories, id);
}

const unsigned char* GameInput_GetCategoryKeyText(const GameInput* self, GameActionCategoryId id)
{
    return Registry_GetKeyText(&self->_categories, id);
}

uint32_t GameInput_GetCategorySource(const GameInput* self, GameActionCategoryId id)
{
    return Registry_GetSource(&self->_categories, id);
}

bool GameInput_TryGetCategoryOverrideSource(const GameInput* self, GameActionCategoryId id, uint32_t* outSourceKeyId)
{
    if ((id >= self->_categoryStates._count) || !GetCategoryStates(self)[id].HasOverride)
    {
        return false;
    }
    *outSourceKeyId = GetCategoryStates(self)[id].Override.Source;
    return true;
}

KeyRegistry* GameInput_GetKeyRegistry(const GameInput* self)
{
    return self->_keys;
}

const char* GameInput_DescribeDisplayNameProblem(const unsigned char* text)
{
    return DescribeTextProblem(text, false, GAME_INPUT_MAX_DISPLAY_NAME_BYTES);
}

const char* GameInput_DescribeDescriptionProblem(const unsigned char* text)
{
    return DescribeTextProblem(text, true, GAME_INPUT_MAX_DESCRIPTION_BYTES);
}

void GameInput_LogActions(const GameInput* self)
{
    if (self->_logger == NULL)
    {
        return;
    }
    GenericBuffer Categories;
    GenericBuffer_AllocateVariable(&Categories, GameActionCategory_Count, sizeof(GameActionCategoryId));
    GenericBuffer Actions;
    GenericBuffer_AllocateVariable(&Actions, INITIAL_ACTION_CAPACITY, sizeof(GameActionId));
    Error FindResult = GameInput_FindCategoriesByPrefix(self, (const unsigned char*)u8"", &Categories);
    Error_Deconstruct(&FindResult);
    UserData Context = UserData_FromPointer((void*)self);
    GenericBuffer_SortAscendingAllocating(&Categories, CompareCategoriesForMenu, &Context);

    LogLine(self, LogLevel_Info, "%" PRIu32 " actions in %" PRIu32 " categories:",
        Registry_GetActiveCount(&self->_actions), Registry_GetActiveCount(&self->_categories));
    const GameActionCategoryId* Ids = (const GameActionCategoryId*)Categories._data;
    for (size_t Index = 0U; Index < Categories._count; Index++)
    {
        GameActionCategoryInfo Info;
        if (!GameInput_TryGetCategoryInfo(self, Ids[Index], &Info))
        {
            continue;
        }
        char SourceText[PRINTABLE_KEY_CAPACITY] = "";
        uint32_t Source = GameInput_GetCategorySource(self, Ids[Index]);
        if (Source != ENGINE_SOURCE)
        {
            snprintf(SourceText, sizeof(SourceText), ", from %s", DescribeSource(self, Source));
        }
        char OverrideText[PRINTABLE_KEY_CAPACITY] = "";
        uint32_t OverrideSource = ENGINE_SOURCE;
        if (GameInput_TryGetCategoryOverrideSource(self, Ids[Index], &OverrideSource))
        {
            snprintf(OverrideText, sizeof(OverrideText), ", changed by %s", DescribeSource(self, OverrideSource));
        }
        LogLine(self, LogLevel_Info, "%s (%s, order %" PRId32 "%s%s):", (const char*)Info.DisplayName,
            (const char*)GameInput_GetCategoryKeyText(self, Ids[Index]), Info.Order, SourceText, OverrideText);
        LogCategoryActions(self, Ids[Index], false, &Actions);
    }

    // Actions whose category was withdrawn.
    bool HasUncategorized = false;
    for (GameActionId Id = 0U; (Id < Registry_GetCount(&self->_actions)) && !HasUncategorized; Id++)
    {
        const ActionEntry* Entry = GetActionEntry(self, Id);
        HasUncategorized = (Entry != NULL) && !GameInput_IsCategoryActive(self, Entry->Category);
    }
    if (HasUncategorized)
    {
        LogLine(self, LogLevel_Info, "Without a category:");
        LogCategoryActions(self, 0U, true, &Actions);
    }
    Memory_Free(Actions._data);
    Memory_Free(Categories._data);
}


// Public functions: action queries.
bool GameInput_IsActionDown(const GameInput* self, GameActionId id)
{
    return IsActionInState(self, id, GAME_INPUT_KEY_STATE_DOWN);
}

bool GameInput_IsActionPressed(const GameInput* self, GameActionId id)
{
    return IsActionInState(self, id, GAME_INPUT_KEY_STATE_PRESSED);
}

bool GameInput_IsActionReleased(const GameInput* self, GameActionId id)
{
    return IsActionInState(self, id, GAME_INPUT_KEY_STATE_RELEASED);
}

bool GameInput_IsActionPressedOrRepeated(const GameInput* self, GameActionId id)
{
    return IsActionInState(self, id, (uint8_t)(GAME_INPUT_KEY_STATE_PRESSED | GAME_INPUT_KEY_STATE_REPEATED));
}

size_t GameInput_GetActionKeys(const GameInput* self, GameActionId id,
    GameKey outKeys[GAME_INPUT_MAX_BINDINGS_PER_ACTION])
{
    const ActionState* State = FindActiveActionState(self, id);
    if (State == NULL)
    {
        return 0U;
    }
    size_t KeyCount = 0;
    for (size_t Index = 0; Index < State->BindingCount; Index++)
    {
        const GameInputBinding* Binding = &State->Bindings[Index];
        if ((GameInput_GetRawKeyState(self, Binding->Key) != 0) && IsBindingMatched(self, Binding))
        {
            outKeys[KeyCount] = Binding->Key;
            KeyCount++;
        }
    }
    return KeyCount;
}

void GameInput_ConsumeAction(GameInput* self, GameActionId id)
{
    GameKey Keys[GAME_INPUT_MAX_BINDINGS_PER_ACTION];
    size_t KeyCount = GameInput_GetActionKeys(self, id, Keys);
    for (size_t Index = 0; Index < KeyCount; Index++)
    {
        GameInput_ConsumeKey(self, Keys[Index]);
    }
}


// Public functions: bindings.
size_t GameInput_GetBindingCount(const GameInput* self, GameActionId id)
{
    const ActionState* State = FindActionState(self, id);
    return (State != NULL) ? State->BindingCount : 0U;
}

bool GameInput_TryGetBinding(const GameInput* self, GameActionId id, size_t index, GameInputBinding* outBinding)
{
    const ActionState* State = FindActionState(self, id);
    if ((State == NULL) || (index >= State->BindingCount))
    {
        return false;
    }
    *outBinding = State->Bindings[index];
    return true;
}

bool GameInput_IsSetByPlayer(const GameInput* self, GameActionId id)
{
    const ActionState* State = FindActionState(self, id);
    return (State != NULL) && State->IsSetByPlayer;
}

Error GameInput_SetBindings(GameInput* self, GameActionId id, const GameInputBinding* bindings, size_t bindingCount)
{
    if ((self == NULL) || ((bindings == NULL) && (bindingCount > 0U)))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "GameInput_SetBindings: self must not be NULL and bindings may be NULL only for 0 bindings.");
    }
    if (bindingCount > GAME_INPUT_MAX_BINDINGS_PER_ACTION)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "GameInput_SetBindings: too many bindings.");
    }
    for (size_t Index = 0; Index < bindingCount; Index++)
    {
        if (!GameInputBinding_IsValid(&bindings[Index]))
        {
            return Error_Construct2(ErrorCode_IllegalArgument, "GameInput_SetBindings: a binding has an invalid key or modifier.");
        }
    }
    const ActionEntry* Entry = GetActionEntry(self, id);
    if (Entry == NULL)
    {
        return Error_Construct4(ErrorCode_InvalidOperation, "GameInput_SetBindings: action %" PRIu32
            " is not registered.", id);
    }

    if ((bindingCount == 0U) && Entry->KeepsABinding)
    {
        RestoreDefaults(self, id);
        LogLine(self, LogLevel_Warning, "\"%s\" always keeps a binding: it keeps its defaults instead of none.",
            (const char*)GameInput_GetActionKeyText(self, id));
    }
    else
    {
        ActionState* State = FindActionState(self, id);
        if (bindingCount > 0U)
        {
            Memory_Copy(bindings, State->Bindings, sizeof(GameInputBinding) * bindingCount);
        }
        State->BindingCount = bindingCount;
        State->IsSetByPlayer = true;
    }
    RebuildKeyIndex(self);
    return Error_CreateSuccess();
}

Error GameInput_ResetActionBindings(GameInput* self, GameActionId id)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "GameInput_ResetActionBindings: self must not be NULL.");
    }
    if (!Registry_IsActive(&self->_actions, id))
    {
        return Error_Construct4(ErrorCode_InvalidOperation, "GameInput_ResetActionBindings: action %" PRIu32
            " is not registered.", id);
    }
    RestoreDefaults(self, id);
    RebuildKeyIndex(self);
    return Error_CreateSuccess();
}

void GameInput_ResetBindings(GameInput* self)
{
    ActionState* States = GetActionStates(self);
    for (size_t Index = 0U; Index < self->_actionStates._count; Index++)
    {
        States[Index].IsSetByPlayer = false;
    }
    RefreshDefaults(self);
    ClearKeptEntries(self);
    RebuildKeyIndex(self);
}

size_t GameInput_GetKeyBindingCount(const GameInput* self, GameKey key)
{
    if (!GameKey_IsValid(key))
    {
        return 0U;
    }
    return self->_keyIndexStarts[(size_t)key + 1U] - self->_keyIndexStarts[key];
}

bool GameInput_TryGetKeyBinding(const GameInput* self, GameKey key, size_t index, GameActionId* outId,
    GameInputBinding* outBinding)
{
    if (index >= GameInput_GetKeyBindingCount(self, key))
    {
        return false;
    }
    const KeyIndexEntry* Entry = &((const KeyIndexEntry*)self->_keyIndex._data)[self->_keyIndexStarts[key] + index];
    *outId = Entry->Action;
    *outBinding = (GameInputBinding){ .Key = key, .Modifiers = Entry->Modifiers };
    return true;
}


// Public functions: the controls file.
Error GameInput_LoadBindingsFromFile(GameInput* self, const unsigned char* path)
{
    if ((self == NULL) || (path == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "GameInput_LoadBindingsFromFile: self and path must not be NULL.");
    }

    // Raw bytes, not ReadAllText: the JSON parser rejects a trailing terminator.
    GenericBuffer Source;
    GenericBuffer_AllocateVariable(&Source, CONTROLS_FILE_INITIAL_CAPACITY, sizeof(unsigned char));
    Error Result = FileSystem_ReadAllBytes(path, &Source);
    if ((Result.Code == ErrorCode_Success) && HasUtf8ByteOrderMark(&Source))
    {
        GenericBuffer_RemoveRange(&Source, 0U, UTF8_BOM_LENGTH); // A hand-edited file may start with one.
    }

    JSONObjectPool* Pool = NULL;
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONObjectPool_Create(&Pool);
    }
    JSONObjectValue Root = JSONObjectValue_CreateNull();
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSON_Deserialize(Pool, &Source, &Root);
    }

    // Every entry is read and checked before anything changes.
    GenericBuffer Staged;
    GenericBuffer_AllocateVariable(&Staged, INITIAL_ACTION_CAPACITY, sizeof(ControlsEntry));
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadControlsDocument(&Root, &Staged);
    }
    if (Result.Code == ErrorCode_Success)
    {
        ControlsEntry* Entries = (ControlsEntry*)Staged._data;
        for (size_t Index = 0U; Index < Staged._count; Index++)
        {
            ApplyControlsEntry(self, &Entries[Index]);
        }
        RebuildKeyIndex(self);
    }
    FreeControlsEntries(&Staged);

    // The values are copied out, so the whole document goes with its pool.
    Result = ReleasePool(Pool, Result);
    Memory_Free(Source._data);
    return Result;
}

Error GameInput_SaveBindingsToFile(const GameInput* self, const unsigned char* path)
{
    if ((self == NULL) || (path == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "GameInput_SaveBindingsToFile: self and path must not be NULL.");
    }

    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, CONTROLS_FILE_INITIAL_CAPACITY, sizeof(unsigned char));
    JSONObjectPool* Pool = NULL;
    Error Result = JSONObjectPool_Create(&Pool);
    if (Result.Code == ErrorCode_Success)
    {
        Result = WriteControlsDocument(self, Pool, &Text);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = FileSystem_WriteAllText(path, Text._data);
    }
    Result = ReleasePool(Pool, Result);
    Memory_Free(Text._data);
    return Result;
}


// Public functions: bindings' text.
bool GameInputBinding_IsValid(const GameInputBinding* binding)
{
    return GameKey_IsValid(binding->Key) && ((binding->Modifiers & (uint8_t)~GAME_KEY_MODIFIER_ALL) == 0);
}

Error GameInputBinding_AppendText(const GameInputBinding* binding, GenericBuffer* outText)
{
    if ((binding == NULL) || (outText == NULL) || (outText->_elementSize != sizeof(unsigned char)))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "GameInputBinding_AppendText: binding and outText must not be NULL and outText must be a byte buffer.");
    }
    if (!GameInputBinding_IsValid(binding))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "GameInputBinding_AppendText: the binding is invalid.");
    }

    // One growing string: drop a terminator already there.
    if ((outText->_count > 0U) && (((const unsigned char*)outText->_data)[outText->_count - 1U] == 0))
    {
        GenericBuffer_SetCount(outText, outText->_count - 1U);
    }

    bool IsWritten = true;
    for (size_t Index = 0; Index < (sizeof(ModifierNames) / sizeof(ModifierNames[0])); Index++)
    {
        if ((binding->Modifiers & ModifierNames[Index].Flag) != 0)
        {
            IsWritten = IsWritten && GenericBuffer_AppendString(outText, ModifierNames[Index].Name)
                && GenericBuffer_AppendByte(outText, BINDING_SEPARATOR);
        }
    }
    IsWritten = IsWritten && GenericBuffer_AppendString(outText, GameKey_GetIdString(binding->Key))
        && GenericBuffer_NullTerminate(outText);
    if (!IsWritten)
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "GameInputBinding_AppendText: the text buffer could not grow.");
    }
    return Error_CreateSuccess();
}

bool GameInputBinding_TryParse(const unsigned char* text, GameInputBinding* outBinding)
{
    if (text == NULL)
    {
        return false;
    }

    uint8_t Modifiers = GAME_KEY_MODIFIER_NONE;
    const unsigned char* Segment = text;
    while (true)
    {
        const unsigned char* Separator = Segment;
        while ((*Separator != 0) && (*Separator != BINDING_SEPARATOR))
        {
            Separator++;
        }
        if (*Separator == 0)
        {
            // The last segment is the key.
            GameKey Key = GameKey_Unknown;
            if (!GameKey_TryParseIdString(Segment, &Key))
            {
                return false;
            }
            *outBinding = (GameInputBinding){ .Key = Key, .Modifiers = Modifiers };
            return true;
        }

        size_t Length = (size_t)(Separator - Segment);
        uint8_t Flag = GAME_KEY_MODIFIER_NONE;
        for (size_t Index = 0; Index < (sizeof(ModifierNames) / sizeof(ModifierNames[0])); Index++)
        {
            const unsigned char* Name = ModifierNames[Index].Name;
            size_t NameLength = 0;
            while (Name[NameLength] != 0)
            {
                NameLength++;
            }
            if ((NameLength == Length) && Memory_IsEqual(Name, Segment, Length))
            {
                Flag = ModifierNames[Index].Flag;
            }
        }
        if ((Flag == GAME_KEY_MODIFIER_NONE) || ((Modifiers & Flag) != 0))
        {
            return false; // Not a modifier, or a modifier given twice.
        }
        Modifiers |= Flag;
        Segment = Separator + 1;
    }
}


// Internal functions (GameInputInternal.h).
uint64_t GameInputInternal_GetMatchVisitCount(void)
{
    return MatchVisitCount;
}
