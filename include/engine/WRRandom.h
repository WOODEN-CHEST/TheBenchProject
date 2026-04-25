#pragma once
#include <stdbool.h>
#include <stdint.h>

#define RANDOM_STATE_LENGTH 4


// Types.
typedef struct RandomStruct
{
    uint64_t _values[RANDOM_STATE_LENGTH];
} Random;

typedef struct RandomStateStruct
{
    uint64_t Values[RANDOM_STATE_LENGTH];
} RandomState;


// Functions.
void Random_Construct1(Random* self);

void Random_Construct2(Random* self, RandomState state);

void Random_Deconstruct1(Random* self);

int32_t Random_NextInt32(Random* self);

int32_t Random_NextInt32InLimit(Random* self, int32_t limit);

int32_t Random_NextInt32InRange(Random* self, int32_t min, int32_t max);

int64_t Random_NextInt64(Random* self);

int64_t Random_NextInt64InLimit(Random* self, int64_t limit);

int64_t Random_NextInt64InRange(Random* self, int64_t min, int64_t max);

float Random_NextSingle(Random* self);

double Random_NextDouble(Random* self);

bool Random_NextBool(Random* self);

void Random_SetState(Random* self, RandomState state);

RandomState Random_GetState(Random* self);

uint64_t Random_NextValue(Random* self);
