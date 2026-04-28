# Project Agent Guidelines

## Tech Stack

- **Language:** C23
- **Renderer / Framework:** Raylib 6.0
- **Compiler flags:** All warnings enabled and treated as errors, pedantic warnings on, conversion warnings on.
  Write code that compiles cleanly under these flags. Never silence a warning with a cast or pragma unless
  there is no other option, and leave a comment explaining why.

---

## Project Layout

```
project/
  compile/      # Build scripts. Do not modify unless changing build configuration.
  include/      # Public header files (.h), one per module.
  source/       # Implementation files (.c), one per module (roughly).
```

Headers go under `include/`, source files go under `source/`. There is no nested directory structure below
these two unless explicitly introduced later.

---

## Build Rules

- The build script lives in the `compile/` directory.
- **Do not run the build script mid-feature.** Building is only done between complete, self-contained features.
- If you need to verify syntax or types during development, you can try to invoke the compiler on just the modules or files
  which you are working on. Compiling the entire project mid-feature likely won't work.

---

## Platform

Linux and Windows. Platform-specific functionality is handled through a strict split between public
headers and implementation files:

- **Public headers** (`include/`) are always platform-agnostic. They define the API only — no
  platform-specific types, macros, or includes.
- **Implementation files** (`source/`) contain platform-specific code, gated with `#ifdef _WIN32` /
  `#elif defined(__linux__)` etc. as needed.
- When implementations diverge significantly, a module may have multiple implementation files (one per
  platform) rather than one file full of ifdefs. Use whichever approach keeps the code cleaner.
- Cross-platform utilities (file paths, OS interactions, etc.) will be provided by the project's own
  framework modules. Use those — do not reimplement or reach for platform-specific equivalents directly.

---

## Code Style

### General

- **C# inspired style.** When in doubt, ask what a C# developer would do and translate that intent into C.
- Braces always on their own line (Allman style).
- Use parentheses to clarify operator precedence when mixing comparison and logical operators.
  - Do: `((a > b) && c)`
  - Don't need them for: `(a && c)` — no ambiguity there.
- Prefer **early returns** to reduce nesting. Deeply nested if-chains should be refactored with guard clauses.
- Try to keep functions at a reasonable size, no 100+ line monoliths.
- Do NOT make any implementation file dependent on symbols from another implementation file. A .c file may only use symbols declared in headers it explicitly includes. Mark all functions and variables in an implementation file that are not declared in any header as static. If implementation files share dependencies that shouldn't be exposed as the project's public API, create new private headers for those members in the source directory (public headers go in include/, private headers in source/).
- When working with a "class" (struct + methods), for field writing or reading purposes check if the class has getters or setters.
  Getters often exist for API stability so that the internal layout can be changed without affecting users.
  Setters often are just used for validation. If you're writing a new class, if the class is very stable then there is no need for getters,
  otherwise you can add static inline getters in the class' header file.

### Naming

| Thing | Convention | Example |
|---|---|---|
| Functions | PascalCase, namespaced | `List_Append`, `Shape_Draw` |
| Types (struct/union/enum/typedef) | PascalCase | `IDrawable`, `EntityList` |
| Union members | PascalCase | `.FloatValue`, `.IntValue` |
| Constants (`#define`, `enum` values) | `UPPERCASE_SNAKE_CASE` (global) or `EnumName_ConstantName` | `MAX_ENTITIES`, `Color_Red` |
| Public struct members | PascalCase | `.Width`, `.Health` |
| Read-only (to outside modules) struct members | `_camelCase` | `._refCount`, `._capacity` |
| Local variables (non-parameter) | PascalCase | `EntityCount`, `Temp` |
| Function parameters | camelCase | `entityCount`, `self` |

**"Public" means accessible to modules outside the one that owns the struct.** Read-only members use the `_camelCase`
prefix as a signal — C has no enforcement, so this is a convention the agent must respect and not bypass.

### `const` Qualification

Use `const` on pointer/reference parameters wherever the pointee is not mutated, mirroring the convention
used in the C standard library. This applies to function parameters and to pointers stored in structs where
applicable:

```c
// self is mutated, name is not.
void Entity_SetName(Entity* self, const char* name);
```

Do **not** apply `const` to struct members (i.e. `const` fields inside a struct body). This causes problems
with late initialization and assignment, so it is banned for struct members.

### Enum Constants

Because C does not require qualifying enum constants with their type name, all enum constants are prefixed with the
enum name to avoid collisions:

```c
typedef enum
{
    Direction_North,
    Direction_South,
    Direction_East,
    Direction_West,
} Direction;
```

### Typedefs

All `struct`, `union`, and `enum` declarations must be accompanied by a `typedef`. Use the pattern:

```c
typedef struct MyStructStruct
{
    ...
} MyStruct;
```

The inner tag name (`MyStructStruct`) is required so the type can be forward-declared in headers if needed.

---

## Module Structure

Each module consists of:
- One header (`include/ModuleName.h`) — public API and type definitions.
- One source file (`source/ModuleName.c`) — implementation.

A source file may contain more than one related "class" if they are tightly coupled. Do not put unrelated classes in
the same file just to reduce file count.

### Source File Member Order

Within a `.c` file, sections appear in this order, each preceded by a comment label:

1. `// Macros.`
2. `// Types.`
3. `// Fields.` (file-scope variables)
4. `// Static functions.`
5. `// Public functions.`

C is order-dependent, so breaking this order is allowed when required (e.g., a static helper needed before a type
that uses it). Do not contort the code to rigidly enforce the order — the order is a guide, not a law.

---

## OOP Conventions

### Interfaces

Interfaces are structs prefixed with `I`. They contain:
1. A pointer to a **vtable struct** (defined separately, also prefixed with `I`, suffixed with `VTable`).
2. A `void* self` pointer to the concrete object.

Because an interface header has no knowledge of any concrete type, vtable function pointers must take
`void*` for the self parameter. This is the one case where `void*` is unavoidable.

The vtable struct holds only function pointers. The interface struct itself is what gets passed around.

```c
typedef struct IDrawableVTableStruct
{
    void (*Draw)(void* self);
    void (*Destroy)(void* self);
} IDrawableVTable;

typedef struct IDrawableStruct
{
    const IDrawableVTable* VTable;
    void* Self;
} IDrawable;
```

**Wrapper functions** (static inline in the header) provide the clean call site:

```c
static inline void IDrawable_Draw(IDrawable self)
{
    self.VTable->Draw(self.Self);
}

static inline void IDrawable_Destroy(IDrawable self)
{
    self.VTable->Destroy(self.Self);
}
```

Callers always go through these wrappers, never through the vtable directly.

Inside a vtable implementation, the concrete type is recovered from `void*` without a cast — in C,
`void*` converts implicitly to and from any object pointer type:

```c
static void Circle_Draw(void* self)
{
    Circle* circleSelf = self;
    // use circleSelf ...
}
```

### Abstract Classes

Abstract classes follow the same vtable pattern as interfaces, but:
- The struct is **not** prefixed with `I`.
- The struct may contain concrete data fields alongside the vtable pointer.
- Because the abstract class type is known to the vtable definition, vtable function pointers take a
  pointer to the abstract class type directly — **not** `void*`. No cast is needed at the call site.

```c
typedef struct ShapeVTableStruct
{
    void (*Draw)(Shape* self);
    void (*Destroy)(Shape* self);
} ShapeVTable;

typedef struct ShapeStruct
{
    const ShapeVTable* VTable;
    Vector2 Position; // concrete shared data
} Shape;
```


### Concrete Implementations

A concrete type implementing an interface or abstract class defines a `static const` vtable and an
upcast function that fills in the interface/abstract struct:

```c
static const IDrawableVTable CIRCLE_DRAWABLE_VTABLE =
{
    .Draw    = Circle_DrawAsDrawable,
    .Destroy = Circle_Destroy,
};

IDrawable Circle_AsDrawable(Circle* self)
{
    return (IDrawable){ .VTable = &CIRCLE_DRAWABLE_VTABLE, .Self = self };
}
```

### Constructors and Destructors

- Constructors are named `TypeName_Construct1`, `TypeName_Construct2`, etc. when multiple exist.
- The destructor is `TypeName_Deconstruct`.
- **Every type must have a `TypeName_Deconstruct`**, even if the current implementation allocates
  nothing. This ensures the hook exists if memory use is added later.
- Factory methods that allocate and return a ready-to-use object use descriptive names like
  `TypeName_Create` or `TypeName_CreateFromFile` — **not** the `ConstructN` naming.
- Every vtable **must** include a `Destroy` function pointer so any holder of an interface can release
  resources without knowing the concrete type.

### Class Methods

The first parameter of any method is the relevant object, named `self` (camelCase, as it is a
parameter):

```c
void List_Append(List* self, int value);
```

## Memory Management

- The project uses a **custom memory module** instead of `malloc`/`free` directly.
-  Use `Memory_Allocate`, `Memory_Reallocate`, `Memory_Free`,
  `Memory_Copy`, `Memory_Move`, `Memory_Set`, and `Memory_Zero` instead of calling the C standard library directly.
- `GenericBuffer` capacity and count are measured in elements, not bytes. The byte/string helpers are only valid when
  `buffer->_elementSize == sizeof(unsigned char)`, and they still must obey the same validation and capacity rules as
  the generic operations. The generic buffer should be used in place of raw buffers where possible. Obviously the
  generic buffer still requires a raw buffer to be passed into it to work, but after that, use the generic buffer.
  The generic buffer should only ever be written to via its write methods. The generic buffer has many validations
  it must make when writing, so if you must manually mutate it may cause issues. If passing
  the byte buffer of the generic buffer is required to a function which will write to them (manual mutation),
  use the generic buffer's TryPrepareForManualMutation, it handles all constraints, ensure the capacity and only
  returns true if the buffer can be mutated in the requested context. Reading from the generic buffer directly
  without its methods is fine, however.
- Functions which write to a generic buffer should NOT clear the buffer beforehand, that is the responsibility of the
  caller of said function.
- This rule is strict: do not call `GenericBuffer_Clear` inside a function just because that function writes to a
  destination buffer. Writer functions append into the buffer's current contents unless the caller explicitly cleared it
  first.
- If a function should behave like overwrite/replace, still do not silently clear inside that function. Either require
  the caller to pass a cleared buffer or expose a separate API whose contract explicitly says it clears/replaces.
- Only clear the generic buffer in a data structure or function if it doesnt make sense for it to have any data beforehand.
- The custom module tracks allocation counts and provides additional utilities, but is otherwise
  semantically equivalent to `malloc`/`free`.
- **Minimize heap fragmentation.** Prefer allocating larger contiguous blocks over many small individual
  allocations. Design data structures with this in mind.
- **Every type must have a `TypeName_Deconstruct`**, even if no memory is currently allocated. This
  ensures the destructor hook exists for when memory use is added later.
- Each object owns its own memory — anything allocated during construction is freed in `Deconstruct`.
- Never free memory you did not allocate. If a pointer is borrowed (not owned), document it with a comment.
- Any interface vtable must expose a `Destroy` slot so callers holding only an interface can release the
  underlying object without knowing its concrete type.

---

## Error Handling

- The project uses a custom error handling system modelled after exceptions (similar to C# / Java) in the WRError module.
An error status is returned via a struct. The struct has an error code (an enum) which is like the type of the error,
and an OPTIONAL error message. The error code can be the success code to indicate no error, at which point the message
should also be null. If the error code is not success, there may be a pointer to an optional error message.
- If you do not see a suitable error code for an error, you can add it.
- Remember that errors with messages need to be freed once no longer used since the message is heap-allocated. 
- Do NOT swallow errors by ignoring them completely. If you feel an error is ignorable and not critical,
at least remember to deconstruct it to free any used memory by it.
---

## Raylib and Header Hygiene

- **Minimize Raylib symbols in public headers.** Prefer forward declarations where possible.
- If a public struct field or function parameter uses a Raylib type unavoidably, include the minimum Raylib header
  needed, and leave a comment noting the dependency.
- Implementation files (`.c`) may include Raylib freely.

---

## Incomplete Work

- When leaving work intentionally incomplete (mid-feature, deferred logic, known gap), mark it with a `// TODO:` comment.
- Do **not** leave uncommented placeholder code or stubs that silently do the wrong thing.
- There is no formal testing step — correctness is verified by code review and eventual build + run between features.

---

## New Modules

- Before creating a new module (new `.h` / `.c` pair), note it explicitly in your response so it is visible in review.
- Do not silently add files. State the new module name and a one-line rationale.

---

## Strings

- The project uses UTF-8 Unicode strings. To make it easier to work with them, all strings are unsigned char arrays instead
of regular char arrays. Since this is Unicode and UTF-8 encoding of it, determining whether a character is something
should be done by extracting its codepoint and testing that, and writing it same way (use codepoint write functions).
- Remember to prefix strings and character literals with the 'u8' prefix.
- If you're dealing with UTF-16 strings (in Windows, for example), then use uint16_t as the unit type for them. However,
generally you should be trying to make sure the project uses UTF-8, whatever API gave UTF-16 strings should have the strings
in usage converted to UTF-8.


## Static state
Avoid static state as much as possible (not illegal, just should be avoided). The C lib already has enough of it. 
All required data is passed to the functions where needed instead of held in global variables.


## Constants
- It is generally preferred to have fields as constants instead of macros, but sometimes that may cause more issues. If it does,
a macro will be fine.
- You should avoid magic numbers and magic constants where possible. Make them constants and use those.


## Git
- You are NOT allowed to run git functions which mutate files, read-only git functions are allowed.


## Comments
- Do not add useless comments everywhere, only comment the super non-obvious, weird or hacky stuff, which should be rare.

# Types
- Be wary of desktop platform dependant code. Use explicit int types and limits from stdint
instead of platform-dependant int types from the language.
Exception is if the API being used also doesn't use explicit int types (like how many OpenGL functions just use int instead of
something like int32_t). Be careful about clib printf formatting too, since some of it is platform dependant too.


## Quick Reference Checklist (for agents before submitting)

- [ ] Compiles cleanly under strict flags (no warnings, no conversions, pedantic-safe)?
- [ ] All new types are `typedef`-ed with an inner tag name?
- [ ] Enum constants use `EnumName_ConstantName` format?
- [ ] Public struct members PascalCase, read-only ones `_camelCase`?
- [ ] Local variables PascalCase, function parameters camelCase?
- [ ] No `const` on struct members (only on pointer parameters where applicable)?
- [ ] Every new interface has a vtable struct, a wrapper struct, and static inline wrapper functions?
- [ ] Interface vtable functions use `void*` for self; abstract class vtable functions use the abstract type?
- [ ] No unnecessary casts from `void*`?
- [ ] Every vtable includes a `Destroy` slot?
- [ ] Every concrete vtable instance is `static const`?
- [ ] Every type has a `Deconstruct`, even if currently empty?
- [ ] Memory allocated in Construct is freed in Deconstruct?
- [ ] Custom memory module used for allocations (or placeholder `// TODO` if not yet available)?
- [ ] No Raylib types leaked into public headers unnecessarily?
- [ ] Public headers are platform-agnostic; platform-specific code is in implementation files only?
- [ ] New modules explicitly noted in response?
- [ ] Incomplete sections marked with `// TODO:`?
- [ ] Generic buffer, if written to, written to via its methods and not directly?
