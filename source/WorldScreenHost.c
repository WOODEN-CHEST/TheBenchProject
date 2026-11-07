#include "WorldScreenHost.h"
#include "GameKey.h"


// Static functions.
/* Consumes the mouse buttons pressed this tick that nothing took: a press on the picture is the screen's. */
static void TakeButtons(GameInput* input)
{
    for (int32_t Key = (int32_t)GameKey_MouseLeft; Key <= (int32_t)GameKey_MouseBack; Key++)
    {
        if (GameInput_IsKeyPressed(input, (GameKey)Key))
        {
            GameInput_ConsumeKey(input, (GameKey)Key);
        }
    }
}

/* Consumes every keyboard key down or released this tick, and the typed text: the screen has the keyboard. */
static void TakeKeyboard(GameInput* input)
{
    for (int32_t Key = 0; Key < (int32_t)GameKey_Count; Key++)
    {
        uint8_t State = GameInput_GetRawKeyState(input, (GameKey)Key);
        if (GameKey_IsKeyboardKey((GameKey)Key)
            && ((State & (GAME_INPUT_KEY_STATE_DOWN | GAME_INPUT_KEY_STATE_RELEASED)) != 0U))
        {
            GameInput_ConsumeKey(input, (GameKey)Key);
        }
    }
    GameInput_ConsumeText(input);
}

/* Whether a key the use action is bound to is down (as the device has it: its press was consumed). */
static bool IsUseKeyDown(const GameInput* input)
{
    size_t Count = GameInput_GetBindingCount(input, GameAction_Use);
    for (size_t Index = 0U; Index < Count; Index++)
    {
        GameInputBinding Binding;
        if (GameInput_TryGetBinding(input, GameAction_Use, Index, &Binding)
            && ((GameInput_GetRawKeyState(input, Binding.Key) & GAME_INPUT_KEY_STATE_DOWN) != 0U))
        {
            return true;
        }
    }
    return false;
}

/* Whether this tick's pointer is on the hosted screen's picture: the player points at it and no UI above claimed the
 * pointer. */
static bool IsPointedAt(const WorldScreenHost* self, const GameInput* input, const WorldScreenHit* pointed)
{
    return (self->_objectId != 0U) && (pointed != NULL) && (pointed->ObjectId == self->_objectId) && pointed->IsOnPicture
        && !GameInput_IsPointerConsumed(input);
}


/* Where the ray goes during a held press off the picture: on the screen itself if it still meets it, or else on its plane. */
static bool TryGetContinuedPointer(const WorldScreenHost* self, const WorldScreenHit* pointed, const WorldScreenHit* continued, Vector2* outPointer)
{
    const WorldScreenHit* Hit = ((pointed != NULL) && (pointed->ObjectId == self->_objectId)) ? pointed : continued;
    if ((Hit == NULL) || (Hit->ObjectId != self->_objectId))
    {
        return false;
    }
    *outPointer = Hit->Picture;
    return true;
}


// Public functions.
void WorldScreenHost_Construct(WorldScreenHost* self, uint64_t objectId)
{
    *self = (WorldScreenHost){ ._objectId = objectId, ._isFocused = false, ._isUseHeld = false, ._isPointed = false,
        ._pointer = { 0 } };
}

void WorldScreenHost_Deconstruct(WorldScreenHost* self)
{
    (void)self;
}

Error WorldScreenHost_Update(WorldScreenHost* self, UIScreen* screen, ProgramTime time, GameInput* input,
    const WorldScreenHit* pointed)
{
    return WorldScreenHost_UpdateContinued(self, screen, time, input, pointed, NULL);
}

Error WorldScreenHost_UpdateContinued(WorldScreenHost* self, UIScreen* screen, ProgramTime time, GameInput* input,
    const WorldScreenHit* pointed, const WorldScreenHit* continued)
{
    if (input == NULL)
    {
        UIScreenInput Empty = UIScreenInput_CreateEmpty();
        self->_isPointed = false;
        return UIScreen_Update(screen, time, &Empty);
    }

    // A held press keeps the screen pointed at, wherever the ray goes.
    bool IsOnPicture = IsPointedAt(self, input, pointed);
    bool IsPressHeld = UIScreen_HasHeldPress(screen);
    self->_isPointed = IsOnPicture || IsPressHeld;
    if (IsOnPicture)
    {
        self->_pointer = pointed->Picture;
    }
    else if (IsPressHeld)
    {
        TryGetContinuedPointer(self, pointed, continued, &self->_pointer);
    }
    if (self->_isFocused && !self->_isPointed)
    {
        self->_isFocused = false; // looked away: the screen gets no input
    }
    if (!self->_isFocused)
    {
        self->_isUseHeld = false;
    }
    if (!self->_isFocused && IsOnPicture)
    {
        if (GameInput_IsActionPressed(input, GameAction_Use))
        {
            // The press that starts using the screen neither acts in the game nor types its letter.
            self->_isFocused = true;
            self->_isUseHeld = true;
            GameInput_ConsumeAction(input, GameAction_Use);
            GameInput_ConsumeText(input);
        }
        else if (GameInput_IsKeyPressed(input, GameKey_MouseLeft))
        {
            self->_isFocused = true; // the click also goes to the UI under the pointer
        }
    }
    else if (self->_isUseHeld)
    {
        // Still held, that key types nothing: what it repeats is not text for the screen.
        self->_isUseHeld = IsUseKeyDown(input);
        if (self->_isUseHeld)
        {
            GameInput_ConsumeText(input);
        }
    }
    size_t TextBefore = GameInput_GetTextCodepointCount(input);

    UIScreenInput ScreenInput =
    {
        .Input = input, .IsPointerPresent = IsOnPicture, .IsPointerTracked = self->_isPointed, .PointerPosition = self->_pointer,
        .HasKeyboardFocus = self->_isFocused
    };
    Error Result = UIScreen_Update(screen, time, &ScreenInput);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    if (self->_isPointed)
    {
        TakeButtons(input);
    }
    if (!self->_isFocused)
    {
        return Error_CreateSuccess();
    }
    // A press that typed text the UI took is typing, never letting go (a pause bound to a letter, typed into a field).
    bool IsTyping = (TextBefore > 0U) && (GameInput_GetTextCodepointCount(input) == 0U);
    if (!IsTyping && (GameInput_IsActionPressed(input, GameAction_UIBack)
        || GameInput_IsActionPressed(input, GameAction_Pause)))
    {
        // An Escape the UI did not use lets go of the screen, and does not reach the game as pause.
        self->_isFocused = false;
        self->_isUseHeld = false;
        GameInput_ConsumeAction(input, GameAction_UIBack);
        GameInput_ConsumeAction(input, GameAction_Pause);
        return Error_CreateSuccess();
    }
    TakeKeyboard(input);
    return Error_CreateSuccess();
}

void WorldScreenHost_Release(WorldScreenHost* self)
{
    self->_isFocused = false;
    self->_isUseHeld = false;
}
