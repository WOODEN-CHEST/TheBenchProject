#pragma once


/**
 * Program time module.
 *
 * A tiny plain value type that carries the two time quantities every update/render tick needs: the
 * total time elapsed since the game loop started, and the time that passed during the current tick
 * (the delta). Both are in seconds.
 *
 * There are effectively two producers of this value. The fixed-timestep update loop fills PassedTime
 * with a constant step (1 / update rate) and TotalTime with the accumulated simulation time, while the
 * render pass fills PassedTime with the real, variable frame delta and TotalTime with the real wall
 * time. Same struct, two clocks.
 *
 * ProgramTime owns no memory and is a trivial value type (like WRFramework's Int32Vector), so it has no
 * constructor/deconstructor pair; copy it freely and build one with ProgramTime_Create.
 */


// Types.
/**
 * @brief The two time quantities delivered to an update or render tick, in seconds.
 *
 * A plain value type owning no heap memory; copy it freely.
 */
typedef struct ProgramTimeStruct
{
    /** @brief Seconds elapsed since the game loop started (monotonic within one clock source). */
    double TotalTime;
    /** @brief Seconds that passed during the current tick (the delta time). */
    double PassedTime;
} ProgramTime;


// Functions.
/**
 * @brief Builds a ProgramTime from a total time and a passed (delta) time.
 * @param totalTime Seconds elapsed since the loop started.
 * @param passedTime Seconds that passed during the current tick.
 * @returns A ProgramTime carrying the two values.
 */
static inline ProgramTime ProgramTime_Create(double totalTime, double passedTime)
{
    return (ProgramTime)
    {
        .TotalTime = totalTime,
        .PassedTime = passedTime
    };
}
