# Logger module

Files: `include/Logger.h`, `source/Logger.c`.

## Purpose

Writes timestamped, level-tagged messages to both a log file (`<base>/logs/latest.log`) and standard
output. It is the single output channel for the game: nothing else should print to stdout/stderr directly
(see the rule in `AGENTS.md`).

`<base>` is the base directory passed to `Logger_Construct` / `Logger_ArchiveLatestLog`: the game passes the
executable's directory (`main.c`), so logs land next to the executable wherever the game is started from.
NULL means the process working directory (handy for small tools).

The module has two parts:

1. **Old-log moving (rotation).** `Logger_ArchiveLatestLog` moves an existing `<base>/logs/latest.log` into
   `<base>/logs/archived/`.
2. **New-log creating and using.** `Logger_Construct` opens a fresh `<base>/logs/latest.log`, and the
   `Logger_Log*` methods write to it (and to stdout).

## Type

`Logger` — a value type the caller owns. It holds an open log-file handle, a (non-owning) stdout
stream, and two reusable scratch buffers so logging does not allocate per call. It is **not** copyable
by value and is **not** thread-safe: log from the main thread only. Code on other threads (e.g. an audio
callback) must never log.

`LogLevel` — `LogLevel_Info`, `LogLevel_Warning`, `LogLevel_Error`, `LogLevel_Critical`. Rendered into
the prefix as `Info` / `Warning` / `Error` / `CRITICAL`.

## Message format

Each message becomes:

```
[<date>][<time>][<level>] <message>\n
```

- Date: `<year>y;<month>m;<day>d` — month/day zero-padded to two digits (e.g. `2026y;07m;05d`).
- Time: `<hour>h:<minute>m:<second>s` — all fields zero-padded to two digits (e.g. `09h:04m:07s`).

The file copy is plain text. The stdout copy is identical but, **on Linux only**, wrapped in an ANSI
color per level: Info = white, Warning = yellow, Error = bright red, Critical = red. On Windows the
stdout copy is written without color (colored Windows output is a future change).

## Rotation / archival

`Logger_ArchiveLatestLog`:

- No-op (success) if `<base>/logs/latest.log` does not exist.
- Otherwise moves the log (one filesystem rename, `FileSystem_MoveEntry`) to `logs/archived/<date>.log`,
  where `<date>` is the **old log's own last-modification date** (formatted like the date prefix above).
- On a name clash, appends a space and the lowest free number: `<date> 2.log`, `<date> 3.log`, ...

`Logger_Construct` calls this automatically before opening the new log. After opening it, it also
**prunes** `logs/archived/`: the oldest archives (by modification time) are deleted until at most 30
remain (`LOGGER_MAX_ARCHIVED_LOGS`). A pruning failure is logged as a warning and does not fail
construction.

## Usage

```c
Logger Log;
Error Result = Logger_Construct(&Log, BaseDirectory._data); // or NULL for the working directory
if (Result.Code != ErrorCode_Success)
{
    // The logger is unavailable; fall back to a raw print and abort.
    Error_Deconstruct(&Result);
    return 1;
}

Error e = Logger_LogInfo(&Log, (const unsigned char*)u8"Started.");
Error_Deconstruct(&e);

e = Logger_LogWarningFormatted(&Log, (const unsigned char*)u8"Retry %d of %d", attempt, max);
Error_Deconstruct(&e);

Error Close = Logger_Deconstruct(&Log);
Error_Deconstruct(&Close);
```

Each level has two methods — a literal-string one (`Logger_LogInfo`) and a printf-style one
(`Logger_LogInfoFormatted`) — plus the level-taking `Logger_Log` / `Logger_LogFormatted`. All log
methods return an `Error`; deconstruct it (logging failures are not fatal to the caller).

The logger is created by `main` as the **first** thing initialized and passed by pointer to whatever
needs to log (no global/static logger instance).

## Rules

- **Never log per tick** from the update loop. Every line is flushed to the file and stdout immediately
  (so a crash never loses it), which is far too slow for hot paths. Log state changes instead: a
  recoverable failure is logged once, when it first happens.
- **Format strings are compile-checked** (GCC). `Logger.h` wraps each `*Formatted` function in a
  same-named macro that type-checks the arguments against the format inside `sizeof` (no code is
  generated); a mismatch is a `-Wformat` error under `-Werror`. `Logger.c` defines
  `LOGGER_IMPLEMENTATION` to opt out of the macros while defining the functions.
- Use only `%s`, `%d` and `%f`. Print 64-bit and `size_t` values as `(uint64_t)` / `(int64_t)` with
  `PRIu64` / `PRId64` from `<inttypes.h>`, because `long` is 32-bit on Windows.

## Dependencies / notes

- Uses `WRDateTime` for timestamps: `DateTime_Now` for message times and `DateTime_FromUnixSeconds`
  (converting the archived log's `time_t` modification time) for the archive filename.
- `Logger.c` uses the C library `vsnprintf` for the `*Formatted` methods, because WRFramework has no
  `va_list`-accepting formatter to forward variadic arguments into.
- The Windows ANSI-color path is intentionally omitted for now; enabling it will require turning on
  virtual-terminal processing on the Windows console (a candidate for a future WRFramework hook).