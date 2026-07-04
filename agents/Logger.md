# Logger module

Files: `include/Logger.h`, `source/Logger.c`.

## Purpose

Writes timestamped, level-tagged messages to both a log file (`logs/latest.log`) and standard output.
It is the single output channel for the game: nothing else should print to stdout/stderr directly (see
the rule in `AGENTS.md`).

The module has two parts:

1. **Old-log moving (rotation).** `Logger_ArchiveLatestLog` moves an existing `logs/latest.log` into
   `logs/archived/`.
2. **New-log creating and using.** `Logger_Construct` opens a fresh `logs/latest.log`, and the
   `Logger_Log*` methods write to it (and to stdout).

## Type

`Logger` — a value type the caller owns. It holds an open log-file handle, a (non-owning) stdout
stream, and two reusable scratch buffers so logging does not allocate per call. It is **not** copyable
by value and is **not** thread-safe (guard it externally if logging from multiple threads).

`LogLevel` — `LogLevel_Info`, `LogLevel_Warning`, `LogLevel_Error`, `LogLevel_Critical`. Rendered into
the prefix as `Info` / `Warning` / `Error` / `CRITICAL`.

## Message format

Each message becomes:

```
[<date>][<time>][<level>] <message>\n
```

- Date: `<year>y,<month>m,<day>d` — month/day zero-padded to two digits (e.g. `2026y,07m,05d`).
- Time: `<hour>h:<minute>m:<second>s` — all fields zero-padded to two digits (e.g. `09h:04m:07s`).

The file copy is plain text. The stdout copy is identical but, **on Linux only**, wrapped in an ANSI
color per level: Info = white, Warning = yellow, Error = bright red, Critical = red. On Windows the
stdout copy is written without color (colored Windows output is a future change).

## Rotation / archival

`Logger_ArchiveLatestLog`:

- No-op (success) if `logs/latest.log` does not exist.
- Otherwise copies the log's contents to `logs/archived/<date>.log`, where `<date>` is the **old log's
  own last-modification date** (formatted like the date prefix above), then deletes the original.
- On a name clash, appends a space and the lowest free number: `<date> 1.log`, `<date> 2.log`, ...

`Logger_Construct` calls this automatically before opening the new log.

## Usage

```c
Logger Log;
Error Result = Logger_Construct(&Log);
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

## Dependencies / notes

- Uses `WRDateTime` for timestamps: `DateTime_Now` for message times and `DateTime_FromUnixSeconds`
  (converting the archived log's `time_t` modification time) for the archive filename.
- `Logger.c` uses the C library `vsnprintf` for the `*Formatted` methods, because WRFramework has no
  `va_list`-accepting formatter to forward variadic arguments into.
- The Windows ANSI-color path is intentionally omitted for now; enabling it will require turning on
  virtual-terminal processing on the Windows console (a candidate for a future WRFramework hook).
- Directory creation composes `FileSystem_GetEntryInfo` + single-level `FileSystem_CreateDirectory`
  (`Logger_EnsureLogDirectories`) rather than `FileSystem_CreateAllDirectories`, because the latter is
  currently broken for **relative** paths in WRFramework (it rejects them as empty/invalid, with a
  corrupted error message — a library memory bug). Once that is fixed, this can revert to a single
  `FileSystem_CreateAllDirectories("logs/archived")` call.
