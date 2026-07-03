# Project Agent Guidelines


## Tech Stack

- **Language:** C23
- **Renderer / Framework:** Raylib 6.0 and WRFramework (custom lib for this project)
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

---

Headers go under `include/`, source files go under `source/`.

## Build Rules

- The build script lives in the `compile/` directory.
- **Do not run the build script mid-feature.** Building is only done between complete, self-contained features.
- If you need to verify syntax or types during development, you can try to invoke the compiler on just the modules or files
  which you are working on. Compiling the entire project mid-feature likely won't work.

---


# WRFramework

The project is a custom library called WRFramework, it adds extra funcionality which raylib doesnt have, at least not fully.
The library is split into modules, each one having a header file and each for a specific task.
The modules are (in alphabetical order):
- WRArrayList: A generic array list.
  - Functions: [construct, ensure total capacity, reserve more capacity, get active buffer, deconstruct]
- WRBinaryIO: Classes and streams for writing and reading binary data.
  - Functions: [BinaryConverter construct, BinaryConverter set target endianness, BinaryConverter write number, BinaryConverter write boolean, BinaryConverter encode variable-length integer, BinaryConverter read number, BinaryConverter read boolean, BinaryConverter decode variable-length integer, BinaryConverter deconstruct, BinaryIOStream construct, BinaryIOStream set target endianness, BinaryIOStream write number, BinaryIOStream write boolean, BinaryIOStream write encoded integer, BinaryIOStream read number, BinaryIOStream read boolean, BinaryIOStream read encoded integer, BinaryIOStream deconstruct]
- WRBufferPool: A pool of generic buffers, so they can be reused instead of constantly allocating new ones.
  - Functions: [construct, borrow, return, deconstruct]
- WRChar: Utilities for writing and reading UTF-8 and UTF-16 unicode characters.
  - Functions: [CharUTF8 is char valid, CharUTF8 is char buffer valid, CharUTF8 is codepoint valid, CharUTF8 get byte count of char, CharUTF8 get byte count of char from end, CharUTF8 get byte count of codepoint, CharUTF8 get codepoint, CharUTF8 get codepoint from end, CharUTF8 write codepoint, CharUTF16 is char valid, CharUTF16 is char buffer valid, CharUTF16 is codepoint valid, CharUTF16 get word count of char, CharUTF16 get word count of char from end, CharUTF16 get word count of codepoint, CharUTF16 get codepoint, CharUTF16 get codepoint from end, CharUTF16 write codepoint]
- WRCollection: Basically Java's ICollection or C#'s IEnumerable interfaces.
  - Functions: [ICollection get enumerator size, ICollection init enumerator, ICollection create enumerator, ICollection write to buffer by value, ICollection write to buffer by reference, CollectionEnumerator has next, CollectionEnumerator get single element size, CollectionEnumerator next by value, CollectionEnumerator next by reference, CollectionEnumerator is reference-returning supported, CollectionEnumerator deconstruct, CollectionEnumerator destroy]
- WRComparator: Generic object comparison operations like in Java.
  - Functions: [compare number, compare string]
- WRCompile: Random compiling utils, like a macro which marks a parameter as unused.
  - Functions: none (provides only the UNUSED macro)
- WREnvironment: Environment properties of the host machine, like newline, directory sperator, endianess, etc.
  - Functions: [get endianness]
- WRError: Exception like errors and their handling functions.
  - Functions: [create success, construct, deconstruct]
- WREvent: An event class which can be subscribed, unsubscribed and listened to. Use this for events.
  - Functions: [construct, deconstruct, subscribe, unsubscribe, unsubscribe all, get subscriber count, raise, get handler event args, get subscriber user data]
- WRFileStream: An IOStream for file system files (used in place of FILE*).
  - Functions: [as IO stream, construct from handle, deconstruct]
- WRFileSystem: File system operations, like opening file streams, iterating files, getting file system entry properties, etc.
  - Functions: [get entries, get files, get directories, get entry info, create directory, create all directories, create file, open file stream, read all text, read all bytes, write all text, write all bytes, delete entry, move entry, rename entry, directory entry enumerator has next, directory entry enumerator next, directory entry enumerator deconstruct, entry info deconstruct]
- WRGHDF: A binary file format to store data in, like game saves and such. Used for saving binary data to the disk.
  - Functions: [GHDF write document, GHDF read document, GHDF create regular type, GHDF create array type, GHDFObjectValue create value, GHDFCompound set value, GHDFCompound get, GHDFCompound get optional, GHDFCompound get verified, GHDFCompound get optional verified, GHDFCompound remove, GHDFCompound clear, GHDFCompound get entry count, GHDFCompound get entry collection, GHDFCompound get value collection, GHDFCompound get key collection, GHDFArray add value, GHDFArray insert value, GHDFArray replace value, GHDFArray remove at, GHDFArray get, GHDFArray clear, GHDFArray copy raw bytes, GHDFArray get element count, GHDFArray get element type, GHDFArray get element collection, GHDFObjectPool create, GHDFObjectPool borrow object, GHDFObjectPool return object, GHDFObjectPool deconstruct]
- WRHash: Hashcode creation functions.
  - Functions: [hash string, hash bytes, hash pointer, hash number]
- WRHashMap: A generic map data structure.
  - Functions: [construct, deconstruct, as map, create default options]
- WRHashSet: An open-addressing hash set of distinct fixed-size elements (stored by value in one contiguous block), driven by caller-supplied hash and comparison callbacks. Implements the ISet interface.
  - Functions: [create default options, as set, construct, deconstruct]
- WRInt32Vector: 32bit integer vector.
  - Functions: none (defines only the Int32Vector struct)
- WRIO: IO streams like in C# and Java.
  - Functions: [get position, set position, seek to start or end, move position, set length, flush, write byte, write bytes, write string, read byte, read bytes, read all, get total size, get remaining size, is seekable, is writable, is readable, is length-settable, is end of stream, close, deconstruct]
- WRJSON: JSON reading, writing and handling.
  - Functions: [JSON serialize, JSON deserialize, JSONObjectValue create value, JSONCompound set, JSONCompound get, JSONCompound get optional, JSONCompound get verified, JSONCompound get optional verified, JSONCompound remove, JSONCompound clear, JSONCompound get entry count, JSONCompound get entry collection, JSONCompound get value collection, JSONCompound get key collection, JSONArray add, JSONArray insert, JSONArray replace, JSONArray remove at, JSONArray get, JSONArray get optional, JSONArray get verified, JSONArray get optional verified, JSONArray clear, JSONArray get element count, JSONArray get element collection, JSONObjectPool create, JSONObjectPool borrow object, JSONObjectPool return object, JSONObjectPool deconstruct]
- WRList: An interface for lists.
  - Functions: [as collection, get enumerator size, init enumerator, create enumerator, get element count, get element size, get flags, is read-only, add last, add first, insert, remove first, remove last, remove at, replace, add range last, add range first, insert range, remove range, replace range, clear, get element, get pointer to element, get first, get last, get scratch size, sort ascending, sort descending, reverse, filter, map, sum, max, min, contains, count where, first index of, last index of, copy to, deconstruct]
- WRMap: An interface for maps.
  - Functions: [get key size, get value size, get entry collection view, get key collection view, get value collection view, get entry count, get flags, is read-only, get element, get pointer to element, add or update, remove, clear, contains key, contains value, deconstruct, default key comparator, default value comparator]
- WRMath: Various math utilities for scalar values.
  - Functions: [remainder, power, log base-10, log base-2, natural log, log arbitrary base, square root, cube root, nth root, sine, cosine, tangent, arcsine, arccosine, arctangent, arctangent of two components, hyperbolic sine, hyperbolic cosine, hyperbolic tangent, inverse hyperbolic sine, inverse hyperbolic cosine, inverse hyperbolic tangent, ceiling, floor, round, truncate, split integral and fractional parts, is NaN, is infinity, is positive infinity, is negative infinity, min, max, clamp, absolute value, sign, lerp, degrees to radians, radians to degrees, equals within tolerance, normalize to unit range, map between ranges, create rounding options]
- WRMemory: Memory management functions, buffer management functions.
  - Functions: [GenericBuffer create variable, GenericBuffer allocate variable, GenericBuffer create constant, GenericBuffer create read-only, GenericBuffer set callback, GenericBuffer clear callback, GenericBuffer ensure total capacity, GenericBuffer reserve more capacity, GenericBuffer get capacity remaining, GenericBuffer is read-only, GenericBuffer is fixed-capacity, GenericBuffer add last, GenericBuffer add first, GenericBuffer insert, GenericBuffer replace, GenericBuffer remove at, GenericBuffer remove first, GenericBuffer remove last, GenericBuffer add last range, GenericBuffer add first range, GenericBuffer insert range, GenericBuffer replace range, GenericBuffer remove range, GenericBuffer get pointer to element, GenericBuffer get pointer to first, GenericBuffer get pointer to last, GenericBuffer get at, GenericBuffer get first, GenericBuffer get last, GenericBuffer clear, GenericBuffer contains, GenericBuffer first index of, GenericBuffer last index of, GenericBuffer get sort scratch size, GenericBuffer reverse, GenericBuffer sort ascending, GenericBuffer sort descending, GenericBuffer filter, GenericBuffer map, GenericBuffer sum, GenericBuffer max, GenericBuffer min, GenericBuffer count where, GenericBuffer append byte, GenericBuffer append range bytes, GenericBuffer append string, GenericBuffer null-terminate, GenericBuffer set byte, GenericBuffer prepare for manual mutation, GenericBuffer set count, GenericBuffer commit count, GenericBuffer get writable tail, Memory allocate, Memory reallocate, Memory free, Memory set, Memory zero, Memory is equal, Memory copy, Memory move, Memory try multiply size, Memory try add size, Memory try grow capacity, Memory get total allocation count, Memory get total reallocation count, Memory get total free count, Memory get current allocation count]
- WRMemoryStream: An IO stream which is backed by memory, not a socket or on the disk.
  - Functions: [as IO stream, construct, get backing buffer, set length, deconstruct]
- WRNumber: Number conversions from and to strings with advanced options.
  - Functions: [parse integer, format integer, parse float, format float, parse double, format double, create decimal format options]
- WRObjectPool: A generic object pool where objects can be borrowed and returned, used to avoid constant allocations.
  - Functions: [construct, deconstruct, get element size, get section capacity, borrow object, return object, clear]
- WRPath: File system path functionality and management.
  - Functions: [change extension, remove extension, get extension, has extension, combine, append, ends in separator, get parent path, get last entry name, get last entry stem, is entry name valid, validate entry name, is valid, validate, get path type, normalize, is normalized, is rooted, is fully qualified, get root, is sub path, trim trailing separator, ensure trailing separator, contains directory segments, split]
- WRRandom: A random number generator.
  - Functions: [construct, deconstruct, next int32, next int32 in limit, next int32 in range, next int64, next int64 in limit, next int64 in range, next float, next double, next bool, next raw value, get state, set state]
- WRRandomShuffledCollection: A "shuffle bag" collection which plays back its items in endless shuffled sequences using an embedded RNG; the ICollection view enumerates items in insertion order without randomness.
  - Functions: [construct, deconstruct, set random, ensure total capacity, add, add range, remove at, clear, set items, get item, get next, reshuffle, get element count, get element size, as collection]
- WRRandomWeightedCollection: A collection of items with attached weights (finite doubles >= 0.0) supporting weighted random picking via a caller-supplied RNG. Optimized for fill-once-pick-many usage (alias method: O(1) picks, lazy O(n) table rebuild after mutations); the ICollection view enumerates items in insertion order without randomness.
  - Functions: [construct, deconstruct, add, add range, remove at, clear, set items, get item, get pointer to item, get weight, set weight, get random index, get random item, get random item pointer, get element count, get element size, get total weight, as collection]
- WRSet: An interface for sets (unordered collections of distinct fixed-size elements), like C#'s ISet, plus generic set-algebra functions that work on any ISet implementation.
  - Functions: [get element size, default element comparator, as collection, get element count, get flags, is read-only, add, remove, clear, contains, deconstruct, union with, except with, intersect with, symmetric except with, overlaps, is subset of, is superset of, set equals]
- WRShuffle: Uniform (Fisher-Yates) in-place shuffling of element containers using a caller-supplied RNG.
  - Functions: [shuffle list, shuffle buffer]
- WRSocket: Sockets for networking operations.
  - Functions: [SocketAddress parse from string, SocketAddress resolve from hostname, SocketAddress get IP, SocketAddress get port, SocketAddress get family, SocketAddress deconstruct, TcpListener create and listen, TcpListener accept connection, TcpListener get local address, TcpListener deconstruct, TcpSocket connect, TcpSocket get IO stream, TcpSocket get local address, TcpSocket get remote address, TcpSocket set receive timeout, TcpSocket set send timeout, TcpSocket shutdown, TcpSocket deconstruct, UdpSocket create, UdpSocket bind, UdpSocket send datagram, UdpSocket receive datagram, UdpSocket set receive timeout, UdpSocket deconstruct]
- WRStandardStream: IO streams for stdin, stdout and stderr.
  - Functions: [as IO stream, create from standard input/output/error, deconstruct]
- WRString: String operations.
  - Functions: [validate encoding, are codepoints defined, is null or empty, is null or whitespace, to lower, to upper, invert case, get byte length, get codepoint length, equals, equals exact, copy to, split, index of, concat, contains, count occurrences, ends with, starts with, format, join, replace, substring, trim, get trim indices, compare, remove, insert, reverse, repeat, get character index array, create split options, create index options]
- WRStringBuilder: A stringbuilder class.
  - Functions: [construct, deconstruct, append string, append string from builder, append substring, append code point, append integer, append float, append double, append boolean, insert string, insert string from builder, insert substring, insert code point, insert integer, insert float, insert double, insert boolean, remove range, truncate, get length, ensure total capacity, reserve more capacity, get backing string, copy to]
- WRThread: Threading operations and functionality.
  - Functions: [Thread create, Thread join, Thread is running, Thread sleep current, Thread get current, Thread deconstruct, Mutex create, Mutex lock, Mutex try lock, Mutex release, Mutex deconstruct, ConditionVariable create, ConditionVariable wait, ConditionVariable wait with timeout, ConditionVariable signal, ConditionVariable broadcast, ConditionVariable deconstruct, ReadWriteLock create, ReadWriteLock lock for read, ReadWriteLock lock for write, ReadWriteLock release read, ReadWriteLock release write, ReadWriteLock deconstruct]
- WRUnicode: Unicode funcionality (including converting and testing codepoints).
  - Functions: [to lower, to upper, is letter, is digit, is number, get numeric value, is symbol, is mark, is separator, is whitespace, is punctuation, is upper, is lower, is cased, is ASCII, is ASCII letter, is ASCII digit, is control, is other category, equals ignoring case, get category, is defined]
- WRUnicodeLoader: Class to load unicode data from a UnicodeData.txt database.
  - Functions: [create empty, load from file, load from stream, deconstruct]
- WRUserData: A fixed 128-byte value type for caller-attached data (holds either a pointer or a small inline value). Header-only.
  - Functions: [create empty, from pointer, get pointer]

Where the framework uses interfaces or abstract classes, it is generally recommended to pass around the non-concrete type
rather than the concrete one. For storing the object in a variable or struct member, it's context dependent, as
using the interface / abstract class type is generally better, but since this is C and allocations matter, sometimes it is better
to use a concrete type directly. Ask about the correct usage before proceeding.


## Platform

Linux and Windows.
If you do decide to implement platform specific code for something, then the
functionality is handled through a strict split between public headers and implementation files:

- **Public headers** (`include/`) are always platform-agnostic. They define the API only — no
  platform-specific types, macros, or includes.
- **Implementation files** (`source/`) contain platform-specific code, gated with `#ifdef _WIN32` /
  `#elif defined(__linux__)` etc. as needed.
- When implementations diverge significantly, a module may have multiple implementation files (one per
  platform) rather than one file full of ifdefs. Use whichever approach keeps the code cleaner.

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
used in the C standard library. This applies to function parameters:

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
- One or more source files (`source/ModuleName.c`) — implementation.

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

```c
typedef struct ShapeVTableStruct
{
    void (*Draw)(void* self);
    void (*Destroy)(void* self);
} ShapeVTable;

typedef struct ShapeStruct
{
    ShapeVTable* VTable;
    Vector2 Position; // concrete shared data
} Shape;
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
-  Use `Memory_Allocate`, `Memory_Reallocate`, `Memory_Free`, These functions abort if they fail, no need to check for
  null returns in call sites.
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
- After a manual write, NEVER assign `buffer->_count` (or `_data`/`_capacity`) directly. Use
  `GenericBuffer_SetCount`, `GenericBuffer_CommitCount` (add N to the count), or `GenericBuffer_GetWritableTail`
  (reserve + get the write pointer, pair with CommitCount). These validate read-only/capacity; direct field
  writes bypass the invariants. To construct an empty buffer, use the constructors, not hand-set fields.
- For size arithmetic that feeds an allocation (count * elementSize, capacity doubling, etc.), use the checked
  helpers `Memory_TryMultiplySize` / `Memory_TryAddSize` / `Memory_TryGrowCapacity` so overflow can't wrap.
- `GenericBuffer_Insert`/`InsertRange` are safe even when the source aliases the buffer's own storage
  (self-append, internal slices) — no need to copy the element out first.
- Scan/sort/reduce helpers on `IList` and `GenericBuffer` (sort, reverse, filter, etc.) come in two forms:
  the plain form takes a caller-owned scratch buffer (size from `IList_GetScratchSize` /
  `GenericBuffer_GetSortScratchSize`) and does NOT allocate per call — PREFER THIS, reusing one scratch buffer.
  The `...Allocating` form allocates the scratch for you; use it only when you would have to allocate anyway.
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
- Never free memory you did not allocate. If a pointer is borrowed (not owned), document it with a comment.
- Any interface vtable must expose a `Destroy` slot so callers holding only an interface can release the
  underlying object without knowing its concrete type.

---

## Error Handling

- The project uses a custom error handling system modelled after exceptions (similar to C# / Java) in the WRError module.
An error status is returned via a struct. The struct has an error code (an enum) which is like the type of the error,
and an OPTIONAL error message. The error code can be the success code to indicate no error, at which point the message
should also be null. If the error code is not success, there may be a pointer to an optional error message.
- If you do not see a suitable error code for an error, it can be added by bringing the issue up before proceeding.
- Remember that errors with messages need to be freed once no longer used since the message is heap-allocated. 
- Do NOT swallow errors by ignoring them completely. If you feel an error is ignorable and not critical,
at least remember to deconstruct it to free any used memory by it.
- Functions that can return errors always have errors as their return value rather than writing to a pointer which
points to an error object.
- **Best-effort teardown / cleanup loops:** when a destructor frees several resources and more than one can
  fail, do NOT overwrite the error variable each iteration (that leaks the previous message) and do NOT
  early-return on the first failure (that abandons the rest). Keep the first error and `Error_Deconstruct` every
  later one, or accumulate them; either way, free every resource. Also do not read `.Code` off a temporary
  `Error` return value without deconstructing it — capture it in a variable first.
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
- **String writers treat the destination as one growing string.** Functions like `StringUTF8_CopyTo`, `Concat`,
  `ToLower`, `Replace`, etc. drop an existing trailing null terminator before appending, so composing into the
  same buffer repeatedly yields one continuous string (not `"a\0b\0"`). Raw byte appends
  (`GenericBuffer_AppendByte`/`AppendRangeBytes`) and `StringUTF8_Split` are unaffected — Split still builds a
  sequence of null-terminated records, addressed by the offsets it returns.


---

## Framework behavior notes

- **Split returns offsets, not pointers.** `StringUTF8_Split` and `Path_Split` append segment bytes to a string
  buffer and write each segment's byte offset into a `size_t` index buffer. Recover a segment with
  `stringBuffer->_data + offset`. (They used to return raw pointers, which dangle when the buffer grows.)
- **Events are reentrant.** A `WREvent` handler may raise the same event again; `WREvent_Raise` snapshots
  subscribers onto a per-raise frame, so recursion is safe. Events are still not thread-safe.
- **Collection enumerators are caller-owned.** `ICollection` exposes `ICollection_GetEnumeratorSize` and
  `ICollection_InitEnumerator(buffer)`: query the size, supply a (reusable or stack) buffer at least that big
  and suitably aligned, iterate, then `CollectionEnumerator_Deconstruct` (which does NOT free the buffer). The
  buffer must outlive the enumerator. `ICollection_CreateEnumerator` + `CollectionEnumerator_Destroy` are the
  allocating convenience pair. (The filesystem `DirectoryEntryEnumerator` is a separate API and is unchanged.)
- **WRUserData** is the vehicle for caller-attached "user data" across the framework — events, threads, object
  pools, hash maps (hash + comparators), and the GenericBuffer/IList scan callbacks all take it. Pass a
  `const UserData*` to callbacks/helpers that use it immediately; structs that must retain it (event
  subscribers, threads, object pools, hash maps) store a `UserData` by value and hand the callback a pointer to
  the stored copy. Never pass the 128-byte struct by value as a function argument. The `GenericBuffer`
  growth-callback context stays a plain `void*` (internal allocator plumbing embedded in every buffer, not a
  user-facing callback).


---

## Static state
Avoid static state as much as possible (not illegal, just should be avoided). The C lib already has enough of it. 
All required data is passed to the functions where needed instead of held in global variables.

---

## Constants
- It is generally preferred to have fields as constants instead of macros, but sometimes that may cause more issues. If it does,
a macro will be fine.
- You should avoid magic numbers and magic constants where possible. Make them constants and use those.

---

## Git
- You are NOT allowed to run git functions which mutate files, read-only git functions are allowed.

---

## Comments
- Do not add useless comments everywhere, only comment the super non-obvious, weird or hacky stuff, which should be rare.

---

## Documentation
Documentation in public headers is MANDATORY. Every public member declared in a header file — functions, structs,
struct fields, unions, enums, enum constants, typedefs, function-pointer types, and macros — must have a
documentation comment. The only exception is a member so trivially obvious that documentation would add nothing;
this exception is rare, so when in doubt, document. Private members (static functions and anything else confined
to implementation files) do not require documentation; document them only where they are non-obvious.

Documentation style (self-contained description — do not assume access to other documented files as reference):
- Use Doxygen-style block comments (`/** ... */`) placed directly above the member they describe.
- Functions: start with a `@brief` one-sentence summary. Where useful, follow with a paragraph explaining the
  semantics and contracts: pointer ownership (owned vs borrowed), lifetime and invalidation rules, allocation
  behavior, complexity on hot paths, thread-safety, and NULL handling. Then write one `@param` per parameter
  (mark output parameters with `[out]`, state NULL rules and minimum buffer sizes) and a `@returns` describing
  the success result and every raised ErrorCode together with what triggers it. If the function forwards errors
  from callees, add: `@note May propagate errors from internal calls; consult the documentation of called
  functions for the full set.`
- Types (structs, enums, unions, typedefs, function-pointer types): a `@brief` plus, where useful, a paragraph
  covering how the type is created, used, and released (constructor/deconstructor pairing, mutability,
  thread-safety). Function-pointer types document their parameters and return value like functions, and state
  the contract an implementation must satisfy.
- Struct fields and enum constants: a short `/** @brief ... */` per member stating what it is and any
  constraints (units such as bytes vs elements, valid ranges, ownership, when the field is valid).
- Module headers: where a module's intended usage is not obvious from its functions alone, add a header-level
  comment block near the top of the file explaining what the module provides and how it is meant to be used.

---

## Types
- Be wary of desktop platform dependant code. Use explicit int types and limits from stdint
instead of platform-dependant int types from the language.
Exception is if the API being used also doesn't use explicit int types (like how many OpenGL functions just use int instead of
something like int32_t). Be careful about clib printf formatting too, since some of it is platform dependant too.