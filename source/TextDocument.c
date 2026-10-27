#include "TextDocument.h"
#include "TextComponentInternal.h"
#include "Logger.h"
#include "wr/WRString.h"
#include "wr/WRMemory.h"


// Macros.
/* Growth granularity of the playback-instance pool; documents bind few animations. */
#define TEXT_DOCUMENT_INSTANCE_POOL_SECTION_CAPACITY ((size_t)4)


// Types.
/* A resolved font name. */
typedef struct FontBindingStruct
{
    /* Owned copy of the name. */
    unsigned char* Name;
    /* Borrowed from the asset manager and held by the document's user; NULL when the name failed to load. */
    GameFont* Font;
} FontBinding;

/* A resolved sprite-animation name. */
typedef struct AnimationBindingStruct
{
    /* Owned copy of the name. */
    unsigned char* Name;
    /* A slot of the document's instance pool, playing the animation held by the document's user; NULL when the name
     * failed to load. */
    SpriteAnimationInstance* Instance;
} AnimationBinding;


// Static functions.
static void KeepFirstError(Error* first, Error incoming)
{
    if ((incoming.Code != ErrorCode_Success) && (first->Code == ErrorCode_Success))
    {
        *first = incoming;
        return;
    }
    Error_Deconstruct(&incoming);
}

static unsigned char* CloneName(const unsigned char* name)
{
    size_t ByteCount = StringUTF8_GetByteLength(name) + 1U;
    unsigned char* Clone = Memory_Allocate(ByteCount);
    Memory_Copy(name, Clone, ByteCount);
    return Clone;
}

static bool NamesEqual(const unsigned char* a, const unsigned char* b)
{
    size_t Length = StringUTF8_GetByteLength(a);
    return (StringUTF8_GetByteLength(b) == Length) && Memory_IsEqual(a, b, Length);
}

static FontBinding* FindFontBinding(TextDocument* self, const unsigned char* name)
{
    for (size_t Index = 0; Index < self->_fontBindings._count; Index++)
    {
        FontBinding* Binding = GenericBuffer_GetPointerToElement(&self->_fontBindings, Index);
        if (NamesEqual(Binding->Name, name))
        {
            return Binding;
        }
    }
    return NULL;
}

static AnimationBinding* FindAnimationBinding(TextDocument* self, const unsigned char* name)
{
    for (size_t Index = 0; Index < self->_animationBindings._count; Index++)
    {
        AnimationBinding* Binding = GenericBuffer_GetPointerToElement(&self->_animationBindings, Index);
        if (NamesEqual(Binding->Name, name))
        {
            return Binding;
        }
    }
    return NULL;
}

// Logs (once: the failed binding is remembered) that a name could not be loaded, and releases the error.
static void ReportLoadFailure(TextDocument* self, const char* kind, const unsigned char* name, Error* error,
    const char* consequence)
{
    if (self->_logger != NULL)
    {
        Error LogResult = Logger_LogWarningFormatted(self->_logger,
            u8"TextDocument: %s \"%s\" could not be loaded (%s); %s.", kind, (const char*)name,
            (error->Message != NULL) ? (const char*)error->Message : "no details", consequence);
        Error_Deconstruct(&LogResult);
    }
    Error_Deconstruct(error);
}

// Pool lifecycle hook: tears down a playback instance when the pool is deconstructed.
static Error DeconstructInstanceCallback(void* object, const UserData* userData)
{
    (void)userData;
    return SpriteAnimationInstance_Deconstruct(object);
}

// raylib's built-in font has no glyphs until InitWindow (and is replaced if the window is ever recreated), so the copy
// is wrapped again whenever raylib's font is not the one it holds. Layouts keep pointing at the same member.
static const GameFont* GetBuiltInFont(TextDocument* self)
{
    Font Current = GetFontDefault();
    const Font* Held = &self->_builtInFont._rayFont;
    if ((Current.glyphs != Held->glyphs) || (Current.glyphCount != Held->glyphCount)
        || (Current.texture.id != Held->texture.id))
    {
        self->_builtInFont = GameFont_Wrap(Current);
    }
    return &self->_builtInFont;
}

// TODO: bindings are kept until the document is destroyed, also after no component names them any more (a document
// whose text cycles through many fonts keeps them all loaded). The document is the only holder under its user, so a
// TextDocument_ReleaseUnusedBindings could release them, but it must also bump the revision: layouts point at bound
// fonts and instances and must be rebuilt before they draw again.
// Resolves a font name to its binding, loading it on first use; *outFont is NULL when the name is unavailable.
static Error BindFont(TextDocument* self, const unsigned char* name, const GameFont** outFont)
{
    *outFont = NULL;
    FontBinding* Existing = FindFontBinding(self, name);
    if (Existing != NULL)
    {
        *outFont = Existing->Font;
        return Error_CreateSuccess();
    }

    FontBinding Binding = { .Name = CloneName(name), .Font = NULL };
    if (self->_assets != NULL)
    {
        GameFont* Loaded = NULL;
        Error LoadResult = AssetManager_LoadFont(self->_assets, name, self->_assetUser, &Loaded);
        if (LoadResult.Code == ErrorCode_Success)
        {
            Binding.Font = Loaded;
        }
        else
        {
            ReportLoadFailure(self, "font", name, &LoadResult, "its text uses the default font");
        }
    }
    if (!GenericBuffer_AddLast(&self->_fontBindings, &Binding))
    {
        Memory_Free(Binding.Name);
        return Error_Construct2(ErrorCode_BufferTooLarge, "TextDocument: could not store a font binding.");
    }
    *outFont = Binding.Font;
    return Error_CreateSuccess();
}

// Loads the named animation and creates the document's playback instance of it; *outInstance stays NULL when the
// animation cannot be loaded (a content problem, logged once). Only internal failures are returned as errors.
static Error CreateAnimationInstance(TextDocument* self, const unsigned char* name, SpriteAnimationInstance** outInstance)
{
    *outInstance = NULL;
    if (self->_assets == NULL)
    {
        return Error_CreateSuccess();
    }

    SpriteAnimation* Animation = NULL;
    Error LoadResult = AssetManager_LoadSpriteAnimation(self->_assets, name, self->_assetUser, &Animation);
    if ((LoadResult.Code == ErrorCode_Success) && (SpriteAnimation_GetFrameCount(Animation) == 0U))
    {
        LoadResult = Error_Construct2(ErrorCode_InvalidAssetData, "the animation has no frames");
    }
    if (LoadResult.Code != ErrorCode_Success)
    {
        ReportLoadFailure(self, "sprite animation", name, &LoadResult, "its sprites draw nothing");
        return Error_CreateSuccess();
    }

    void* Slot = NULL;
    Error SlotResult = ObjectPool_GetNewObject(&self->_instancePool, &Slot);
    if (SlotResult.Code != ErrorCode_Success)
    {
        return SlotResult;
    }
    Error CreateResult = SpriteAnimation_CreateInstance(Animation, Slot);
    if (CreateResult.Code != ErrorCode_Success)
    {
        // CreateInstance leaves the slot deconstructed (zeroed), which the pool's teardown hook handles.
        Error DisposeResult = ObjectPool_DisposeObject(&self->_instancePool, Slot);
        Error_Deconstruct(&DisposeResult);
        ReportLoadFailure(self, "sprite animation", name, &CreateResult, "its sprites draw nothing");
        return Error_CreateSuccess();
    }
    *outInstance = Slot;
    return Error_CreateSuccess();
}


// Public functions: lifecycle.
Error TextDocument_Construct(TextDocument* self, TextComponentFactory* factory, AssetManager* assets, Logger* logger)
{
    if ((self == NULL) || (factory == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "TextDocument_Construct: self and factory must not be NULL.");
    }

    AssetUserID User = ASSET_USER_ID_INVALID;
    if (assets != NULL)
    {
        Error UserResult = AssetManager_GetNewUserID(assets, &User);
        if (UserResult.Code != ErrorCode_Success)
        {
            return UserResult;
        }
    }

    ObjectPoolLifecycle Lifecycle = { .ConstructObject = NULL, .ResetObject = NULL,
        .DeconstructObject = DeconstructInstanceCallback };
    Error PoolResult = ObjectPool_Construct2(&self->_instancePool, sizeof(SpriteAnimationInstance),
        TEXT_DOCUMENT_INSTANCE_POOL_SECTION_CAPACITY, Lifecycle, NULL);
    if (PoolResult.Code != ErrorCode_Success)
    {
        if (assets != NULL)
        {
            Error RetireResult = AssetManager_RetireUser(assets, User);
            Error_Deconstruct(&RetireResult);
        }
        return PoolResult;
    }

    self->_factory = factory;
    self->_assets = assets;
    self->_logger = logger;
    self->_assetUser = User;
    self->_root = NULL;
    self->_defaultFontName = NULL;
    GenericBuffer_AllocateVariable(&self->_fontBindings, 0U, sizeof(FontBinding));
    GenericBuffer_AllocateVariable(&self->_animationBindings, 0U, sizeof(AnimationBinding));
    self->_builtInFont = GameFont_Wrap(GetFontDefault());
    self->_revision = 1U;
    return Error_CreateSuccess();
}

Error TextDocument_Deconstruct(TextDocument* self)
{
    // A deconstructed document has no factory: a second call is a no-op.
    if ((self == NULL) || (self->_factory == NULL))
    {
        return Error_CreateSuccess();
    }

    Error First = Error_CreateSuccess();
    if (self->_root != NULL)
    {
        TextComponent_SetIsHeld(self->_root, false);
        KeepFirstError(&First, TextComponentFactory_ReturnTree(self->_factory, self->_root));
        self->_root = NULL;
    }

    // The instances first: they point into animations held by the document's asset user.
    KeepFirstError(&First, ObjectPool_Deconstruct(&self->_instancePool));
    for (size_t Index = 0; Index < self->_fontBindings._count; Index++)
    {
        FontBinding* Binding = GenericBuffer_GetPointerToElement(&self->_fontBindings, Index);
        Memory_Free(Binding->Name);
    }
    for (size_t Index = 0; Index < self->_animationBindings._count; Index++)
    {
        AnimationBinding* Binding = GenericBuffer_GetPointerToElement(&self->_animationBindings, Index);
        Memory_Free(Binding->Name);
    }
    Memory_Free(self->_fontBindings._data);
    GenericBuffer_AllocateVariable(&self->_fontBindings, 0U, sizeof(FontBinding));
    Memory_Free(self->_animationBindings._data);
    GenericBuffer_AllocateVariable(&self->_animationBindings, 0U, sizeof(AnimationBinding));
    Memory_Free(self->_defaultFontName);
    self->_defaultFontName = NULL;

    if (self->_assets != NULL)
    {
        KeepFirstError(&First, AssetManager_RetireUser(self->_assets, self->_assetUser));
        self->_assets = NULL;
    }
    self->_factory = NULL;
    return First;
}


// Public functions: the tree.
Error TextDocument_SetRoot(TextDocument* self, TextComponent* root)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "TextDocument_SetRoot: self must not be NULL.");
    }
    if (root == self->_root)
    {
        return Error_CreateSuccess();
    }
    if (root != NULL)
    {
        if (TextComponent_GetFactory(root) != self->_factory)
        {
            return Error_Construct2(ErrorCode_IllegalArgument,
                "TextDocument_SetRoot: the root was created by another factory than the document's.");
        }
        if (TextComponent_GetParent(root) != NULL)
        {
            return Error_Construct2(ErrorCode_InvalidOperation,
                "TextDocument_SetRoot: the component has a parent; only a root can become a document's tree.");
        }
        if (TextComponent_IsHeld(root))
        {
            return Error_Construct2(ErrorCode_InvalidOperation,
                "TextDocument_SetRoot: the component is already the tree of another document.");
        }
    }

    TextComponent* Previous = self->_root;
    self->_root = root;
    if (root != NULL)
    {
        TextComponent_SetIsHeld(root, true);
    }
    self->_revision++;
    if (Previous != NULL)
    {
        TextComponent_SetIsHeld(Previous, false);
        return TextComponentFactory_ReturnTree(self->_factory, Previous);
    }
    return Error_CreateSuccess();
}

Error TextDocument_TakeRoot(TextDocument* self, TextComponent** outRoot)
{
    if ((self == NULL) || (outRoot == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "TextDocument_TakeRoot: self and outRoot must not be NULL.");
    }
    *outRoot = self->_root;
    if (self->_root != NULL)
    {
        TextComponent_SetIsHeld(self->_root, false);
        self->_root = NULL;
        self->_revision++;
    }
    return Error_CreateSuccess();
}


// Public functions: fonts.
Error TextDocument_SetDefaultFontName(TextDocument* self, const unsigned char* fontName)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "TextDocument_SetDefaultFontName: self must not be NULL.");
    }
    if ((fontName != NULL) && !AssetManager_IsValidAssetName(fontName))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "TextDocument_SetDefaultFontName: the font name must be an asset name ([a-z][a-z0-9_.-]*).");
    }

    Memory_Free(self->_defaultFontName);
    self->_defaultFontName = (fontName != NULL) ? CloneName(fontName) : NULL;
    self->_revision++;
    return Error_CreateSuccess();
}


// Public functions: binding.
Error TextDocument_ResolveFont(TextDocument* self, const unsigned char* fontName, const GameFont** outFont)
{
    if ((self == NULL) || (outFont == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "TextDocument_ResolveFont: self and outFont must not be NULL.");
    }
    *outFont = NULL;

    const GameFont* Resolved = NULL;
    if (fontName != NULL)
    {
        Error Result = BindFont(self, fontName, &Resolved);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    if ((Resolved == NULL) && (self->_defaultFontName != NULL))
    {
        Error Result = BindFont(self, self->_defaultFontName, &Resolved);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    *outFont = (Resolved != NULL) ? Resolved : GetBuiltInFont(self);
    return Error_CreateSuccess();
}

Error TextDocument_ResolveAnimation(TextDocument* self, const unsigned char* animationName,
    SpriteAnimationInstance** outInstance)
{
    if ((self == NULL) || (outInstance == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextDocument_ResolveAnimation: self and outInstance must not be NULL.");
    }
    *outInstance = NULL;
    if (animationName == NULL)
    {
        return Error_CreateSuccess();
    }

    AnimationBinding* Existing = FindAnimationBinding(self, animationName);
    if (Existing != NULL)
    {
        *outInstance = Existing->Instance;
        return Error_CreateSuccess();
    }

    AnimationBinding Binding = { .Name = NULL, .Instance = NULL };
    Error Result = CreateAnimationInstance(self, animationName, &Binding.Instance);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Binding.Name = CloneName(animationName);
    if (!GenericBuffer_AddLast(&self->_animationBindings, &Binding))
    {
        Memory_Free(Binding.Name);
        if (Binding.Instance != NULL)
        {
            Error InstanceResult = SpriteAnimationInstance_Deconstruct(Binding.Instance);
            Error_Deconstruct(&InstanceResult);
            Error DisposeResult = ObjectPool_DisposeObject(&self->_instancePool, Binding.Instance);
            Error_Deconstruct(&DisposeResult);
        }
        return Error_Construct2(ErrorCode_BufferTooLarge, "TextDocument: could not store an animation binding.");
    }
    *outInstance = Binding.Instance;
    return Error_CreateSuccess();
}

const unsigned char* TextDocument_ResolveTranslation(const TextDocument* self, const unsigned char* key)
{
    // TODO: string tables (translations beyond the translate component are still to be designed).
    (void)self;
    (void)key;
    return NULL;
}


// Public functions: ticking.
Error TextDocument_Update(TextDocument* self, ProgramTime time)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "TextDocument_Update: self must not be NULL.");
    }

    // By index: an event handler may bind another animation, which can move the binding list.
    for (size_t Index = 0; Index < self->_animationBindings._count; Index++)
    {
        AnimationBinding* Binding = GenericBuffer_GetPointerToElement(&self->_animationBindings, Index);
        SpriteAnimationInstance* Instance = Binding->Instance;
        if (Instance == NULL)
        {
            continue;
        }
        Error Result = SpriteAnimationInstance_Update(Instance, time.PassedTime);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    return Error_CreateSuccess();
}
