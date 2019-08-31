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
	return cpu.doubleSpeed ? (500000000 / CLOCK_RATE) : (1000000000 / CLOCK_RATE);
}

inline int64_t NanoTime()
{
	return std::chrono::high_resolution_clock::now().time_since_epoch().count();
}

inline int64_t NanoExecTime()
{
	timespec ts;
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
	return (int64_t)ts.tv_sec * 1000000 + (int64_t)ts.tv_nsec;
}

void QueueInterrupt(int index);
