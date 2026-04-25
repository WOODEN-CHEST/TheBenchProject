#include "WRRandom.h"
#include "WRMemory.h"

#include <stdint.h>
#include <time.h>


// Types.
typedef union FloatMaskUnion
{
    uint32_t IntValue;
    float FloatValue;
} FloatMask;

typedef union DoubleMaskUnion
{
    uint64_t IntValue;
    double DoubleValue;
} DoubleMask;


// Static functions.
/* 
* The RNG algorithm used here is xoshiro256**.
* https://en.wikipedia.org/wiki/Xorshift#xoshiro256**
*/
static inline uint64_t Rotate(uint64_t x, int32_t k)
{
    return (x << (uint32_t)k) | (x >> (uint32_t)(64 - k));
}

static bool IsAllZeroState(const uint64_t* values)
{
    return ((values[0] == 0u) &&
        (values[1] == 0u) &&
        (values[2] == 0u) &&
        (values[3] == 0u));
}

static uint64_t SplitMix64(uint64_t* state)
{
    *state += UINT64_C(0x9E3779B97F4A7C15);

    uint64_t Result = *state;
    Result = (Result ^ (Result >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    Result = (Result ^ (Result >> 27)) * UINT64_C(0x94D049BB133111EB);
    return Result ^ (Result >> 31);
}

static uint64_t SeedFromEntropy(const Random* self)
{
    const uint64_t TimeBits = (uint64_t)time(NULL);
    const uint64_t AddressBits = (uint64_t)(uintptr_t)self;
    return TimeBits ^ Rotate(AddressBits | UINT64_C(1), 17);
}

static void SetSeededState(Random* self, uint64_t seed)
{
    uint64_t State = seed;
    for (int32_t Index = 0; Index < RANDOM_STATE_LENGTH; Index++)
    {
        self->_values[Index] = SplitMix64(&State);
    }
}

static uint64_t NextValue(Random* random)
{
    uint64_t* Values = random->_values;

    const uint64_t Result = Rotate(Values[1] * 5, 7) * 9;

    const uint64_t T = Values[1] << 17;
    Values[2] ^= Values[0];
    Values[3] ^= Values[1];
    Values[1] ^= Values[2];
    Values[0] ^= Values[3];
    Values[2] ^= T;
    Values[3] = Rotate(Values[3], 45);

    return Result;
}

static uint32_t NextUInt32InLimit(Random* self, uint32_t limit)
{
    uint32_t Bits;
    uint32_t Value;
    const uint32_t RejectThreshold = UINT32_MAX - (UINT32_MAX % limit);
    do
    {
        Bits = (uint32_t)NextValue(self);
        Value = Bits % limit;
    } while (Bits >= RejectThreshold);

    return Value;
}

static uint64_t NextUInt64InLimit(Random* self, uint64_t limit)
{
    uint64_t Bits;
    uint64_t Value;
    const uint64_t RejectThreshold = UINT64_MAX - (UINT64_MAX % limit);
    do
    {
        Bits = NextValue(self);
        Value = Bits % limit;
    } while (Bits >= RejectThreshold);

    return Value;
}


// Functions.
void Random_Construct1(Random* self)
{
    const uint64_t Seed = SeedFromEntropy(self);
    SetSeededState(self, Seed);

    const int32_t SettleIterationCount = 10;
    for (int32_t Index = 0; Index < SettleIterationCount; Index++)
    {
        NextValue(self);
    }
}

void Random_Construct2(Random* self, RandomState state)
{
    Memory_Zero(self, sizeof(*self));
    Random_SetState(self, state);
}

void Random_Deconstruct1(Random* self)
{
    Memory_Zero(self, sizeof(*self));
}

int32_t Random_NextInt32(Random* self)
{
    return (int32_t)NextValue(self);
}

int32_t Random_NextInt32InLimit(Random* self, int32_t limit)
{
    if (limit <= 0)
    {
        return 0;
    }

    return NextUInt32InLimit(self, (uint32_t)limit);
}

int32_t Random_NextInt32InRange(Random* self, int32_t min, int32_t max)
{
    if (max <= min)
    {
        return min;
    }
    return min + (NextUInt32InLimit(self, max - min));
}

int64_t Random_NextInt64(Random* self)
{
    return (int64_t)NextValue(self);
}

int64_t Random_NextInt64InLimit(Random* self, int64_t limit)
{
    if (limit <= 0)
    {
        return 0;
    }

    return NextUInt64InLimit(self, limit);
}

int64_t Random_NextInt64InRange(Random* self, int64_t min, int64_t max)
{
    if (max <= min)
    {
        return min;
    }
    return min + NextUInt64InLimit(self, max - min);
}

float Random_NextSingle(Random* self)
{
    const uint64_t Bits = NextValue(self);
    const int32_t BitCountInMantissa = 23;
    FloatMask Value = { .IntValue = 0u };
    const uint32_t ExponentValue = 0b01111111;
    const uint32_t MantissaValue = (uint32_t)(Bits >> (64 - BitCountInMantissa));

    Value.IntValue = (ExponentValue << BitCountInMantissa) | MantissaValue;

    return Value.FloatValue - 1.0f;
}

double Random_NextDouble(Random* self)
{
    const uint64_t Bits = NextValue(self);
    const int32_t BitCountInMantissa = 52;
    DoubleMask Value = { .IntValue = 0u };
    const uint64_t ExponentValue = 0b01111111111;
    const uint64_t MantissaValue = Bits >> (64 - BitCountInMantissa);

    Value.IntValue = (ExponentValue << BitCountInMantissa) | MantissaValue;
    
    return Value.DoubleValue - 1.0;
}

bool Random_NextBool(Random* self)
{
    return (NextValue(self) & ((uint64_t)1 << 63)) ? true : false;
}

void Random_SetState(Random* self, RandomState state)
{
    for (int32_t Index = 0; Index < RANDOM_STATE_LENGTH; Index++)
    {
        self->_values[Index] = state.Values[Index];
    }

    if (IsAllZeroState(self->_values))
    {
        SetSeededState(self, UINT64_C(1));
    }
}

RandomState Random_GetState(Random* self)
{
    RandomState State;
    for (int32_t Index = 0; Index < RANDOM_STATE_LENGTH; Index++)
    {
        State.Values[Index] = self->_values[Index];
    }
    return State;
}

uint64_t Random_NextValue(Random* self)
{
    return NextValue(self);
}
