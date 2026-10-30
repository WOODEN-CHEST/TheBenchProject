// The world test frame's weather (WorldTestWeather.h): the weather system on the test world's climate, fed by the frame's
// clock, and its debug keys. Test content only.

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include "WorldTestWeather.h"
#include "wr/WRMemory.h"


// Macros.
/** The speed steps End cycles through, times the climate's own speed. */
#define SPEED_STEP_COUNT 3
/** The forecast the backslash logs: how far ahead and in what steps, game hours. */
#define FORECAST_HOURS 24.0
#define FORECAST_STEP_HOURS 1.0
/** Radians to degrees, for the log. */
#define DEGREES_PER_RADIAN 57.29577951308232


// Fields.
static const float SpeedSteps[SPEED_STEP_COUNT] = { 1.0f, 10.0f, 100.0f };
static const WeatherMode ModeCycle[] = { WeatherMode_Simulated, WeatherMode_Fixed, WeatherMode_Scripted, WeatherMode_Off };


// Static functions.
static void Log(const WorldTestWeather* self, const char* format, ...)
{
    if (self->_logger == NULL)
    {
        return;
    }
    char Text[512];
    va_list Arguments;
    va_start(Arguments, format);
    int Length = vsnprintf(Text, sizeof(Text), format, Arguments);
    va_end(Arguments);
    if (Length < 0)
    {
        return;
    }
    Error LogResult = Logger_LogInfoFormatted(self->_logger, (const unsigned char*)u8"WorldTest: %s", Text);
    Error_Deconstruct(&LogResult);
}

/* Logs a failed call (test content: nothing else to do about it); true when it succeeded. */
static bool Check(const WorldTestWeather* self, Error result, const char* what)
{
    if (result.Code == ErrorCode_Success)
    {
        return true;
    }
    Log(self, "weather: %s failed: %s", what, (result.Message != NULL) ? (const char*)result.Message : "no reason given");
    Error_Deconstruct(&result);
    return false;
}

/* A direction's compass-like angle in degrees, 0 toward +Z, 90 toward +X. */
static double GetHeadingDegrees(Vector3 direction)
{
    double Degrees = atan2((double)direction.x, (double)direction.z) * DEGREES_PER_RADIAN;
    return (Degrees < 0.0) ? Degrees + 360.0 : Degrees;
}

/* Insert: the climate's next preset in its file's order, its values at once: a held clock moves no value, and the key
 * shows the preset either way. */
static void MoveToNextPreset(WorldTestWeather* self)
{
    const Climate* Running = WeatherSystem_GetClimate(&self->_system);
    if ((Running == NULL) || !WeatherSystem_IsStarted(&self->_system))
    {
        Log(self, "weather: no climate runs.");
        return;
    }
    const WeatherSimulation* Simulation = WeatherSystem_GetSimulation(&self->_system);
    if (Simulation->Mode == WeatherMode_Off)
    {
        Log(self, "weather: off, the world file's weather stays (Page Down steps the mode).");
        return;
    }
    const ClimatePreset* Next = Climate_GetPreset(Running,
        (uint16_t)((Simulation->Preset + 1U) % Climate_GetPresetCount(Running)));
    if (!Check(self, WeatherSystem_SetPreset(&self->_system, Next->Name, 0.0f), "the next preset"))
    {
        return;
    }
    float Targets[WeatherQuantity_Count];
    Memory_Copy(Simulation->Targets, Targets, sizeof(Targets));
    (void)Check(self, WeatherSystem_SetValuesNow(&self->_system, World_GetEnvironment(self->_world), Next->TargetMask,
        Targets), "the preset's values");
}

/* End: the next speed step. */
static void StepSpeed(WorldTestWeather* self)
{
    const Climate* Running = WeatherSystem_GetClimate(&self->_system);
    if (Running == NULL)
    {
        return;
    }
    self->_speedStep = (self->_speedStep + 1) % SPEED_STEP_COUNT;
    float Speed = Climate_GetSettings(Running)->Speed * SpeedSteps[self->_speedStep];
    if (Check(self, WeatherSystem_SetSpeed(&self->_system, Speed), "the speed"))
    {
        Log(self, "weather speed x%.0f (%.1f x the game clock).", (double)SpeedSteps[self->_speedStep], (double)Speed);
    }
}

/* Page Down: the next mode; off gives the world file's weather back. */
static void StepMode(WorldTestWeather* self)
{
    WeatherMode Current = WeatherSystem_GetSimulation(&self->_system)->Mode;
    size_t Count = sizeof(ModeCycle) / sizeof(ModeCycle[0]);
    size_t Index = 0U;
    while ((Index < Count) && (ModeCycle[Index] != Current))
    {
        Index++;
    }
    WeatherMode Next = ModeCycle[(Index + 1U) % Count];
    if (!Check(self, WeatherSystem_SetMode(&self->_system, Next), "the mode"))
    {
        return;
    }
    if (Next == WeatherMode_Off)
    {
        WeatherValues_ToEnvironment(&self->_authored, World_GetEnvironment(self->_world));
        Log(self, "weather off: the world file's weather is back.");
    }
}

/* The backslash: the next 24 hours, a line an hour. */
static void LogForecast(const WorldTestWeather* self, const GameClock* clock)
{
    const Climate* Running = WeatherSystem_GetClimate(&self->_system);
    if (Running == NULL)
    {
        Log(self, "weather: no climate runs, no forecast.");
        return;
    }
    GenericBuffer Steps;
    GenericBuffer_AllocateVariable(&Steps, (size_t)FORECAST_HOURS, sizeof(WeatherForecastStep));
    if (Check(self, WeatherSystem_Forecast(&self->_system, World_GetEnvironment(self->_world), clock, FORECAST_HOURS,
        FORECAST_STEP_HOURS, &Steps), "the forecast"))
    {
        Log(self, "weather forecast, the next %.0f game hours (* a preset the forecast gets wrong: its accuracy):",
            FORECAST_HOURS);
        const WeatherForecastStep* Entries = (const WeatherForecastStep*)Steps._data;
        for (size_t Index = 0U; Index < Steps._count; Index++)
        {
            const WeatherForecastStep* Entry = &Entries[Index];
            int Hour = (int)(Entry->SecondsOfDay / 3600.0);
            int Minute = (int)(fmod(Entry->SecondsOfDay, 3600.0) / 60.0);
            Log(self, "  %02d:%02d %s%s: clouds %.2f, rain %.2f, wet %.2f, wind %.1f m/s, fog %.3f, %.1f C.", Hour, Minute,
                (const char*)Climate_GetPreset(Running, Entry->Preset)->Name, Entry->IsBlurred ? "*" : "",
                (double)Entry->Values.Quantities[WeatherQuantity_CloudCoverage],
                (double)Entry->Values.Quantities[WeatherQuantity_Precipitation], (double)Entry->Values.Wetness,
                (double)Entry->Values.Quantities[WeatherQuantity_WindSpeed],
                (double)Entry->Values.Quantities[WeatherQuantity_FogDensity],
                (double)Entry->Values.Quantities[WeatherQuantity_Temperature]);
        }
    }
    Memory_Free(Steps._data);
}


// Public functions.
void WorldTestWeather_Construct(WorldTestWeather* self, World* world, AssetManager* assets, Logger* logger)
{
    Memory_Zero(self, sizeof(*self));
    self->_world = world;
    self->_logger = logger;
    self->_authored = WeatherValues_FromEnvironment(World_GetEnvironment(world));
    self->_isConstructed = Check(self, WeatherSystem_Construct(&self->_system, assets, logger), "starting the weather");
}

void WorldTestWeather_Deconstruct(WorldTestWeather* self)
{
    if (self->_isConstructed)
    {
        WeatherSystem_Deconstruct(&self->_system);
        self->_isConstructed = false;
    }
}

void WorldTestWeather_Update(WorldTestWeather* self, const GameClock* clock, double gameSeconds, double realSeconds)
{
    if (!self->_isConstructed)
    {
        return;
    }
    WeatherSystem_Update(&self->_system, World_GetEnvironment(self->_world), clock, gameSeconds, realSeconds);
    size_t Count = 0U;
    const WeatherStrike* Strikes = WeatherSystem_GetStrikes(&self->_system, &Count);
    for (size_t Index = 0U; Index < Count; Index++)
    {
        Log(self, "lightning %.1f km away toward %.0f degrees (its thunder %.1f s after the flash).",
            (double)Strikes[Index].Distance / 1000.0, GetHeadingDegrees(Strikes[Index].Direction),
            (double)(Strikes[Index].Distance / WEATHER_SYSTEM_SPEED_OF_SOUND));
    }
    WeatherSystem_ClearStrikes(&self->_system);
}

void WorldTestWeather_HandleInput(WorldTestWeather* self, const GameInput* input, const GameClock* clock)
{
    if (!self->_isConstructed)
    {
        return;
    }
    if (GameInput_IsKeyPressed(input, GameKey_Insert))
    {
        MoveToNextPreset(self);
    }
    if (GameInput_IsKeyPressed(input, GameKey_Home))
    {
        WeatherSystem_SetLocked(&self->_system, !WeatherSystem_GetSimulation(&self->_system)->IsLocked);
    }
    if (GameInput_IsKeyPressed(input, GameKey_End))
    {
        StepSpeed(self);
    }
    if (GameInput_IsKeyPressed(input, GameKey_PageUp))
    {
        WeatherSystem_Strike(&self->_system, clock);
    }
    if (GameInput_IsKeyPressed(input, GameKey_PageDown) && WeatherSystem_IsStarted(&self->_system))
    {
        StepMode(self);
    }
    if (GameInput_IsKeyPressed(input, GameKey_Backslash))
    {
        LogForecast(self, clock);
    }
}

void WorldTestWeather_LogStatus(const WorldTestWeather* self)
{
    if (!self->_isConstructed)
    {
        return;
    }
    const WeatherSystem* System = &self->_system;
    const ClimatePreset* Preset = WeatherSystem_GetPreset(System);
    const WeatherSimulation* Simulation = WeatherSystem_GetSimulation(System);
    const WorldEnvironment* Environment = World_GetEnvironment(self->_world);
    if (Preset == NULL)
    {
        Log(self, "weather: not running (the climate \"%s\" is not loaded).", (const char*)Environment->Climate);
        return;
    }
    Log(self, "weather: \"%s\" of the climate \"%s\" (%s%s, x%.0f speed, %.2f hours left); clouds %.2f (type %.2f, "
        "density %.2f), rain %.2f, wet %.2f, wind %.1f m/s toward %.0f degrees (gust %.1f), fog %.3f (%.0f m), %.1f C, "
        "flash %.2f.", (const char*)Preset->Name, (const char*)Environment->Climate,
        (const char*)WeatherMode_GetName(Simulation->Mode), Simulation->IsLocked ? ", locked" : "",
        (double)SpeedSteps[self->_speedStep], Simulation->PresetHoursLeft, (double)Environment->CloudCoverage,
        (double)Environment->CloudType, (double)Environment->CloudDensity, (double)Environment->Precipitation,
        (double)Environment->Wetness, (double)Environment->WindSpeed,
        fmod((double)Environment->WindDirection * DEGREES_PER_RADIAN + 360.0, 360.0), (double)Environment->WindGust,
        (double)Environment->FogDensity, (double)Environment->FogHeight, (double)Environment->Temperature,
        (double)Environment->LightningFlash);
}
