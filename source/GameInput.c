#include "GameInput.h"
#include "GameKeyInternal.h"
#include <inttypes.h>
#include "wr/WRChar.h"
#include "wr/WRFileSystem.h"
#include "wr/WRJSON.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"
#include "wr/WRUnicode.h"
#include "raylib/raylib.h"


// Macros.
/** JSON key of the object that maps action ids to binding lists in a controls file. */
#define CONTROLS_KEY_BINDINGS ((const unsigned char*)u8"bindings")
/** Joins the modifiers and the key of a binding's text form. */
#define BINDING_SEPARATOR ((unsigned char)'+')
/** Initial capacity (bytes) for a controls file's contents; the buffer grows as needed. */
#define CONTROLS_FILE_INITIAL_CAPACITY ((size_t)1024)
/** Length of the UTF-8 byte order mark some editors put at the start of a text file. */
#define UTF8_BOM_LENGTH ((size_t)3)
/* Default-binding table entry builder. */
#define BINDING(key, modifiers) { .Key = (key), .Modifiers = (modifiers) }


// Types.
/* Everything known about one action; GameActionInfos is indexed by GameAction. */
typedef struct GameActionInfoStruct
{
    const unsigned char* IdString;
    const unsigned char* DisplayName;
    GameInputBinding Defaults[GAME_INPUT_MAX_BINDINGS_PER_ACTION];
    size_t DefaultCount;
} GameActionInfo;

/* A modifier's name in a binding's text form. */
typedef struct ModifierNameStruct
{
    uint8_t Flag;
    const unsigned char* Name;
} ModifierName;

/* Every action's bindings, as staged while a controls file is read (applied only when the whole file is valid). */
typedef struct BindingTableStruct
{
    GameInputBinding Bindings[GameAction_Count][GAME_INPUT_MAX_BINDINGS_PER_ACTION];
    size_t Counts[GameAction_Count];
} BindingTable;


// Fields.
/* Indexed by GameAction with designated initializers. The id strings are stored in controls files: never rename one. */
static const GameActionInfo GameActionInfos[GameAction_Count] =
{
    [GameAction_UIUp] = { .IdString = u8"ui_up", .DisplayName = u8"Menu Up",
        .Defaults = { BINDING(GameKey_Up, GAME_KEY_MODIFIER_NONE) }, .DefaultCount = 1 },
    [GameAction_UIDown] = { .IdString = u8"ui_down", .DisplayName = u8"Menu Down",
        .Defaults = { BINDING(GameKey_Down, GAME_KEY_MODIFIER_NONE) }, .DefaultCount = 1 },
    [GameAction_UILeft] = { .IdString = u8"ui_left", .DisplayName = u8"Menu Left",
        .Defaults = { BINDING(GameKey_Left, GAME_KEY_MODIFIER_NONE) }, .DefaultCount = 1 },
    [GameAction_UIRight] = { .IdString = u8"ui_right", .DisplayName = u8"Menu Right",
        .Defaults = { BINDING(GameKey_Right, GAME_KEY_MODIFIER_NONE) }, .DefaultCount = 1 },
    [GameAction_UINext] = { .IdString = u8"ui_next", .DisplayName = u8"Next",
        .Defaults = { BINDING(GameKey_Tab, GAME_KEY_MODIFIER_NONE) }, .DefaultCount = 1 },
    [GameAction_UIPrevious] = { .IdString = u8"ui_previous", .DisplayName = u8"Previous",
        .Defaults = { BINDING(GameKey_Tab, GAME_KEY_MODIFIER_SHIFT) }, .DefaultCount = 1 },
    [GameAction_UIAccept] = { .IdString = u8"ui_accept", .DisplayName = u8"Accept",
        .Defaults = { BINDING(GameKey_Enter, GAME_KEY_MODIFIER_NONE), BINDING(GameKey_KeypadEnter, GAME_KEY_MODIFIER_NONE) },
        .DefaultCount = 2 },
    [GameAction_UIBack] = { .IdString = u8"ui_back", .DisplayName = u8"Back",
        .Defaults = { BINDING(GameKey_Escape, GAME_KEY_MODIFIER_NONE) }, .DefaultCount = 1 },
    [GameAction_MoveForward] = { .IdString = u8"move_forward", .DisplayName = u8"Move Forward",
        .Defaults = { BINDING(GameKey_W, GAME_KEY_MODIFIER_NONE) }, .DefaultCount = 1 },
    [GameAction_MoveBack] = { .IdString = u8"move_back", .DisplayName = u8"Move Back",
        .Defaults = { BINDING(GameKey_S, GAME_KEY_MODIFIER_NONE) }, .DefaultCount = 1 },
    [GameAction_MoveLeft] = { .IdString = u8"move_left", .DisplayName = u8"Move Left",
        .Defaults = { BINDING(GameKey_A, GAME_KEY_MODIFIER_NONE) }, .DefaultCount = 1 },
    [GameAction_MoveRight] = { .IdString = u8"move_right", .DisplayName = u8"Move Right",
        .Defaults = { BINDING(GameKey_D, GAME_KEY_MODIFIER_NONE) }, .DefaultCount = 1 },
    [GameAction_MoveUp] = { .IdString = u8"move_up", .DisplayName = u8"Move Up",
        .Defaults = { BINDING(GameKey_Space, GAME_KEY_MODIFIER_NONE) }, .DefaultCount = 1 },
    [GameAction_MoveDown] = { .IdString = u8"move_down", .DisplayName = u8"Move Down",
        .Defaults = { BINDING(GameKey_LeftControl, GAME_KEY_MODIFIER_NONE) }, .DefaultCount = 1 },
    [GameAction_Sprint] = { .IdString = u8"sprint", .DisplayName = u8"Sprint",
        .Defaults = { BINDING(GameKey_LeftShift, GAME_KEY_MODIFIER_NONE) }, .DefaultCount = 1 },
    [GameAction_Jump] = { .IdString = u8"jump", .DisplayName = u8"Jump",
        .Defaults = { BINDING(GameKey_Space, GAME_KEY_MODIFIER_NONE) }, .DefaultCount = 1 },
    [GameAction_Crouch] = { .IdString = u8"crouch", .DisplayName = u8"Crouch",
        .Defaults = { BINDING(GameKey_LeftControl, GAME_KEY_MODIFIER_NONE) }, .DefaultCount = 1 },
    [GameAction_Use] = { .IdString = u8"use", .DisplayName = u8"Use",
        .Defaults = { BINDING(GameKey_E, GAME_KEY_MODIFIER_NONE) }, .DefaultCount = 1 },
    [GameAction_Pause] = { .IdString = u8"pause", .DisplayName = u8"Pause",
        .Defaults = { BINDING(GameKey_Escape, GAME_KEY_MODIFIER_NONE) }, .DefaultCount = 1 },
};

/* The modifiers in the order a binding's text form lists them. */
static const ModifierName ModifierNames[] =
{
    { .Flag = GAME_KEY_MODIFIER_CONTROL, .Name = u8"control" },
    { .Flag = GAME_KEY_MODIFIER_ALT, .Name = u8"alt" },
    { .Flag = GAME_KEY_MODIFIER_SHIFT, .Name = u8"shift" },
    { .Flag = GAME_KEY_MODIFIER_SUPER, .Name = u8"super" },
};


// Static functions: helpers.
static bool IsActionValid(GameAction action)
{
    return ((int32_t)action >= 0) && ((int32_t)action < (int32_t)GameAction_Count);
}

static bool AreStringsEqual(const unsigned char* a, const unsigned char* b)
{
    bool AreEqual = false;
    Error Result = StringUTF8_EqualsExact(a, b, &AreEqual);
    Error_Deconstruct(&Result);
    return AreEqual;
}

static bool AreModifiersIn(uint8_t heldModifiers, uint8_t modifiers)
{
    return (heldModifiers & modifiers) == modifiers;
}

/* Whether a binding matches the held modifiers: every modifier it asks for is held, and no binding (of any action) on the
 * same key asks for more of the held ones. The most specific binding wins, so Shift+Tab is not also a plain Tab, whichever
 * actions they are bound to. */
static bool IsBindingMatched(const GameInput* self, const GameInputBinding* binding)
{
    uint8_t Held = GameInput_GetHeldModifiers(self);
    if (!AreModifiersIn(Held, binding->Modifiers))
    {
        return false;
    }
    for (int32_t ActionIndex = 0; ActionIndex < (int32_t)GameAction_Count; ActionIndex++)
    {
        for (size_t Index = 0; Index < self->_bindingCounts[ActionIndex]; Index++)
        {
            const GameInputBinding* Other = &self->_bindings[ActionIndex][Index];
            bool IsMoreSpecific = (Other->Modifiers != binding->Modifiers) && AreModifiersIn(Other->Modifiers, binding->Modifiers);
            if ((Other->Key == binding->Key) && IsMoreSpecific && AreModifiersIn(Held, Other->Modifiers))
            {
                return false;
            }
        }
    }
    return true;
}

/* Whether any binding of the action has its (unconsumed) key in one of the states in @p stateMask and matches the held
 * modifiers. */
static bool IsActionInState(const GameInput* self, GameAction action, uint8_t stateMask)
{
    if (!IsActionValid(action))
    {
        return false;
    }
    for (size_t Index = 0; Index < self->_bindingCounts[action]; Index++)
    {
        const GameInputBinding* Binding = &self->_bindings[action][Index];
        if (((GameInput_GetKeyState(self, Binding->Key) & stateMask) != 0) && IsBindingMatched(self, Binding))
        {
            return true;
        }
    }
    return false;
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

static bool HasUtf8ByteOrderMark(const GenericBuffer* bytes)
{
    const unsigned char* Data = bytes->_data;
    return (bytes->_count >= UTF8_BOM_LENGTH) && (Data[0] == 0xEFU) && (Data[1] == 0xBBU) && (Data[2] == 0xBFU);
}

static void CopyDefaultBindings(GameInput* self, GameAction action)
{
    const GameActionInfo* Info = &GameActionInfos[action];
    Memory_Copy(Info->Defaults, self->_bindings[action], sizeof(GameInputBinding) * Info->DefaultCount);
    self->_bindingCounts[action] = Info->DefaultCount;
}


// Static functions: controls files.
/* Reads one action's binding list (a JSON array of binding strings) into @p outBindings. */
static Error ReadBindingList(const JSONObjectValue* listValue, GameAction action, GameInputBinding* outBindings,
    size_t* outCount)
{
    const char* ActionId = (const char*)GameActionInfos[action].IdString;
    if (listValue->Type != JSONValueType_Array)
    {
        return Error_Construct4(ErrorCode_InvalidJSON, "Controls file: \"%s\" must be a list of bindings.", ActionId);
    }

    JSONArray* List = listValue->Value.Array;
    size_t Count = JSONArray_GetElementCount(List);
    if (Count > GAME_INPUT_MAX_BINDINGS_PER_ACTION)
    {
        return Error_Construct4(ErrorCode_InvalidJSON, "Controls file: \"%s\" has %" PRIu64 " bindings; at most %" PRIu64
            " are allowed.", ActionId, (uint64_t)Count, (uint64_t)GAME_INPUT_MAX_BINDINGS_PER_ACTION);
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
                (uint64_t)Index, ActionId);
        }

        // WRJSON strings are not terminated: copy into a terminated buffer (binding texts are short).
        const GenericBuffer* Source = Element.Value.String;
        unsigned char Text[GAME_INPUT_MAX_BINDING_TEXT_BYTES + 1];
        if (Source->_count > GAME_INPUT_MAX_BINDING_TEXT_BYTES)
        {
            return Error_Construct4(ErrorCode_InvalidJSON, "Controls file: binding %" PRIu64 " of \"%s\" is too long.",
                (uint64_t)Index, ActionId);
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
        if (HasEmbeddedTerminator || !GameInputBinding_TryParse(Text, &outBindings[Index]))
        {
            return Error_Construct4(ErrorCode_InvalidJSON, "Controls file: \"%s\" in \"%s\" is not a binding (modifiers "
                "then a key id, joined by '+'; see references/controls_structure.md).", (const char*)Text, ActionId);
        }
    }

    *outCount = Count;
    return Error_CreateSuccess();
}

/* Stages the bindings a parsed controls document lists on top of @p table. */
static Error ReadControlsDocument(const JSONObjectValue* root, BindingTable* table)
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

    // Look up each known action, so names that are not actions are ignored (and older files keep loading).
    for (int32_t ActionIndex = 0; ActionIndex < (int32_t)GameAction_Count; ActionIndex++)
    {
        GameAction Action = (GameAction)ActionIndex;
        JSONObjectValue ListValue;
        Result = JSONCompound_GetOptional(BindingsValue.Value.Compound, GameActionInfos[Action].IdString, &ListValue,
            &WasFound);
        if ((Result.Code == ErrorCode_Success) && WasFound)
        {
            Result = ReadBindingList(&ListValue, Action, table->Bindings[Action], &table->Counts[Action]);
        }
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    return Error_CreateSuccess();
}

/* Adds one action's bindings to @p bindings as a JSON array of binding strings. */
static Error WriteBindingList(const GameInput* self, GameAction action, JSONObjectPool* pool, JSONCompound* bindings)
{
    JSONArray* List = NULL;
    Error Result = JSONObjectPool_BorrowArray(pool, &List);
    for (size_t Index = 0; (Result.Code == ErrorCode_Success) && (Index < self->_bindingCounts[action]); Index++)
    {
        unsigned char TextStorage[GAME_INPUT_MAX_BINDING_TEXT_BYTES + 1];
        GenericBuffer Text;
        GenericBuffer_CreateConstant(&Text, TextStorage, sizeof(TextStorage), sizeof(unsigned char), 0);
        Result = GameInputBinding_AppendText(&self->_bindings[action][Index], &Text);

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
        Result = JSONCompound_Set(bindings, GameActionInfos[action].IdString, &ListValue);
    }
    return Result; // Anything borrowed and not attached is reclaimed when the caller deconstructs the pool.
}

/* Builds the controls document for every action and serializes it into @p outText. */
static Error WriteControlsDocument(const GameInput* self, JSONObjectPool* pool, GenericBuffer* outText)
{
    JSONCompound* Root = NULL;
    JSONCompound* Bindings = NULL;
    Error Result = JSONObjectPool_BorrowCompound(pool, &Root);
    if (Result.Code == ErrorCode_Success)
    {
        Result = JSONObjectPool_BorrowCompound(pool, &Bindings);
    }
    for (int32_t ActionIndex = 0; (Result.Code == ErrorCode_Success) && (ActionIndex < (int32_t)GameAction_Count); ActionIndex++)
    {
        Result = WriteBindingList(self, (GameAction)ActionIndex, pool, Bindings);
    }
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


// Public functions: lifecycle.
Error GameInput_Construct(GameInput* self)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "GameInput_Construct: self must not be NULL.");
    }
    Memory_Zero(self, sizeof(*self));
    GameInputSample_Clear(&self->_sample);
    GameInput_ResetBindings(self);
    return Error_CreateSuccess();
}

Error GameInput_Deconstruct(GameInput* self)
{
    if (self != NULL)
    {
        Memory_Zero(self, sizeof(*self));
    }
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


// Public functions: actions.
bool GameInput_IsActionDown(const GameInput* self, GameAction action)
{
    return IsActionInState(self, action, GAME_INPUT_KEY_STATE_DOWN);
}

bool GameInput_IsActionPressed(const GameInput* self, GameAction action)
{
    return IsActionInState(self, action, GAME_INPUT_KEY_STATE_PRESSED);
}

bool GameInput_IsActionReleased(const GameInput* self, GameAction action)
{
    return IsActionInState(self, action, GAME_INPUT_KEY_STATE_RELEASED);
}

bool GameInput_IsActionPressedOrRepeated(const GameInput* self, GameAction action)
{
    return IsActionInState(self, action, (uint8_t)(GAME_INPUT_KEY_STATE_PRESSED | GAME_INPUT_KEY_STATE_REPEATED));
}

size_t GameInput_GetActionKeys(const GameInput* self, GameAction action, GameKey outKeys[GAME_INPUT_MAX_BINDINGS_PER_ACTION])
{
    if (!IsActionValid(action))
    {
        return 0U;
    }
    size_t KeyCount = 0;
    for (size_t Index = 0; Index < self->_bindingCounts[action]; Index++)
    {
        const GameInputBinding* Binding = &self->_bindings[action][Index];
        if ((GameInput_GetRawKeyState(self, Binding->Key) != 0) && IsBindingMatched(self, Binding))
        {
            outKeys[KeyCount] = Binding->Key;
            KeyCount++;
        }
    }
    return KeyCount;
}

void GameInput_ConsumeAction(GameInput* self, GameAction action)
{
    GameKey Keys[GAME_INPUT_MAX_BINDINGS_PER_ACTION];
    size_t KeyCount = GameInput_GetActionKeys(self, action, Keys);
    for (size_t Index = 0; Index < KeyCount; Index++)
    {
        GameInput_ConsumeKey(self, Keys[Index]);
    }
}

size_t GameInput_GetBindingCount(const GameInput* self, GameAction action)
{
    return IsActionValid(action) ? self->_bindingCounts[action] : 0U;
}

bool GameInput_TryGetBinding(const GameInput* self, GameAction action, size_t index, GameInputBinding* outBinding)
{
    if (!IsActionValid(action) || (index >= self->_bindingCounts[action]))
    {
        return false;
    }
    *outBinding = self->_bindings[action][index];
    return true;
}

Error GameInput_SetBindings(GameInput* self, GameAction action, const GameInputBinding* bindings, size_t bindingCount)
{
    if ((self == NULL) || !IsActionValid(action) || ((bindings == NULL) && (bindingCount > 0U)))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "GameInput_SetBindings: self must not be NULL, the action must be valid and bindings may be NULL only for 0 bindings.");
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
    if (bindingCount > 0U)
    {
        Memory_Copy(bindings, self->_bindings[action], sizeof(GameInputBinding) * bindingCount);
    }
    self->_bindingCounts[action] = bindingCount;
    return Error_CreateSuccess();
}

void GameInput_ResetBindings(GameInput* self)
{
    for (int32_t ActionIndex = 0; ActionIndex < (int32_t)GameAction_Count; ActionIndex++)
    {
        CopyDefaultBindings(self, (GameAction)ActionIndex);
    }
}

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

    BindingTable Staged;
    Memory_Copy(self->_bindings, Staged.Bindings, sizeof(Staged.Bindings));
    Memory_Copy(self->_bindingCounts, Staged.Counts, sizeof(Staged.Counts));
    if (Result.Code == ErrorCode_Success)
    {
        Result = ReadControlsDocument(&Root, &Staged);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Memory_Copy(Staged.Bindings, self->_bindings, sizeof(Staged.Bindings));
        Memory_Copy(Staged.Counts, self->_bindingCounts, sizeof(Staged.Counts));
    }

    // The values are copied out, so the whole document goes with its pool.
    if (Pool != NULL)
    {
        Error PoolResult = JSONObjectPool_Deconstruct(Pool);
        if ((Result.Code == ErrorCode_Success) && (PoolResult.Code != ErrorCode_Success))
        {
            Result = PoolResult;
        }
        else
        {
            Error_Deconstruct(&PoolResult);
        }
    }
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

    if (Pool != NULL)
    {
        Error PoolResult = JSONObjectPool_Deconstruct(Pool);
        if ((Result.Code == ErrorCode_Success) && (PoolResult.Code != ErrorCode_Success))
        {
            Result = PoolResult;
        }
        else
        {
            Error_Deconstruct(&PoolResult);
        }
    }
    Memory_Free(Text._data);
    return Result;
}


// Public functions: names.
const unsigned char* GameAction_GetIdString(GameAction action)
{
    return IsActionValid(action) ? GameActionInfos[action].IdString : NULL;
}

const unsigned char* GameAction_GetDisplayName(GameAction action)
{
    return IsActionValid(action) ? GameActionInfos[action].DisplayName : NULL;
}

bool GameAction_TryParseIdString(const unsigned char* idString, GameAction* outAction)
{
    if (idString == NULL)
    {
        return false;
    }
    for (int32_t ActionIndex = 0; ActionIndex < (int32_t)GameAction_Count; ActionIndex++)
    {
        if (AreStringsEqual(GameActionInfos[ActionIndex].IdString, idString))
        {
            *outAction = (GameAction)ActionIndex;
            return true;
        }
    }
    return false;
}

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
