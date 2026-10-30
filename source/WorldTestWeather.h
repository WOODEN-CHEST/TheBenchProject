#pragma once
#include <stdbool.h>
#include "AssetManager.h"
#include "GameClock.h"
#include "GameInput.h"
#include "Logger.h"
#include "WeatherSystem.h"
#include "World.h"

/*
 * Private header (source/) of the world test frame's weather: the weather system running the climate the test world
 * names, fed by the frame's clock every tick, and its debug keys. Test content only: WorldTestFrame.c calls it from its
 * tick.
 *
 * Keys (raw debug keys, read in Update after the UI had first refusal): Insert moves into the climate's next preset in
 * its file's order (over a quarter of a weather hour), Home locks and unlocks the weather, End steps its speed (x1, x10,
 * x100 the climate's own), Page Up strikes lightning now, Page Down steps the mode (simulated, fixed, scripted, off;
 * off gives the world file's weather back) and the backslash logs a forecast of the next 24 game hours. Each is logged,
 * and so is every strike (its distance and direction).
 */


// Types.
/** The test frame's weather. Borrowed pointers stay valid while the frame lives. */
typedef struct WorldTestWeatherStruct
{
    World* _world;
    Logger* _logger;
    /** The weather, constructed while _isConstructed. */
    WeatherSystem _system;
    bool _isConstructed;
    /** The world file's weather, as it was loaded: what the off mode gives back. */
    WeatherValues _authored;
    /** The speed step: 0 the climate's own, 1 ten times it, 2 a hundred times. */
    int _speedStep;
} WorldTestWeather;


// Functions.
/** Sets up the weather over a loaded world (borrowed, as the asset manager and the logger are): it remembers the world
 *  file's weather; the first update loads the climate and starts it. A failure is logged and leaves the frame without
 *  weather. */
void WorldTestWeather_Construct(WorldTestWeather* self, World* world, AssetManager* assets, Logger* logger);

/** Releases the weather (its climate). */
void WorldTestWeather_Deconstruct(WorldTestWeather* self);

/** The tick's weather: moves it by the clock's game seconds and the real seconds of play (0 while the clock is paused),
 *  then logs and clears the strikes it queued. */
void WorldTestWeather_Update(WorldTestWeather* self, const GameClock* clock, double gameSeconds, double realSeconds);

/** The tick's debug keys (Insert, Home, End, Page Up, Page Down, the backslash). */
void WorldTestWeather_HandleInput(WorldTestWeather* self, const GameInput* input, const GameClock* clock);

/** Logs the weather's state: the preset, the mode, the speed, the lock and the values (with the renderer's status, I). */
void WorldTestWeather_LogStatus(const WorldTestWeather* self);
