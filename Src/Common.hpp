#pragma once

#include <chrono>
#include <atomic>

#include "CPU.hpp"

extern bool devMode;
extern bool cgbMode;
extern bool verboseMode;

constexpr int CLOCK_RATE = 4194304;

inline int64_t NSPerClockCycle()
{
	return cpu.doubleSpeed ? (500000000LL / CLOCK_RATE) : (1000000000LL / CLOCK_RATE);
}

inline int64_t NanoTime()
{
	return std::chrono::high_resolution_clock::now().time_since_epoch().count();
}

void QueueInterrupt(int index);
