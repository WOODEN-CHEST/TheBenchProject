# ProgramTime module

Files: `include/ProgramTime.h` (header-only, no `.c`).

## Purpose

A tiny plain value type carrying the two time quantities every update/render tick needs:

- `TotalTime` — seconds since the game loop started.
- `PassedTime` — seconds that passed during the current tick (the delta).

Both are `double`, in seconds.

## Two producers

The same struct is filled by two clocks:

- The **fixed-timestep update loop** fills `PassedTime` with a constant step (`1 / UPDATE_RATE_HZ`) and
  `TotalTime` with the accumulated simulation time.
- The **render pass** fills `PassedTime` with the real, variable frame delta and `TotalTime` with the
  real wall time (`GetTime()`).

## Usage

```c
ProgramTime UpdateTime = ProgramTime_Create(totalSimTime, 1.0 / 480.0);
ProgramTime RenderTime = ProgramTime_Create(GetTime(), realFrameDelta);
```

## Notes

- It owns no memory and is a trivial value type (like WRFramework's `Int32Vector`), so — by that same
  precedent — it has **no** `Construct`/`Deconstruct` pair. Copy it freely; build one with
  `ProgramTime_Create`.
