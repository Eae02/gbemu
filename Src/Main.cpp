#include <SDL.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <fstream>
#include <cstring>
#include <atomic>
#include <thread>

#include "CPU.hpp"
#include "GPU.hpp"
#include "Memory.hpp"
#include "Input.hpp"
#include "Common.hpp"
#include "DebugPane.hpp"
#include "Audio.hpp"

using namespace std::chrono;

void HandleInputEvent(SDL_Event& event);

static constexpr uint32_t CYCLES_PER_TIMER_INC[] =
{
	CLOCK_RATE / 4096,
	CLOCK_RATE / 262144,
	CLOCK_RATE / 65536,
	CLOCK_RATE / 16384
};

static std::atomic_bool shouldQuit;
static bool speedDevPrint;

bool cgbMode;
bool devMode;
bool verboseMode;
bool fastMode;

std::mutex pendingInterruptsMutex;
uint32_t pendingInterrupts;

void QueueInterrupt(int index)
{
	std::lock_guard<std::mutex> lock(pendingInterruptsMutex);
	pendingInterrupts |= 1U << index;
}

void CPUThreadTarget()
{
	uint32_t elapsedCycles = 0;
	uint32_t cyclesSinceTimerInc = 0;
	bool timerOverflow = false;
	
	int64_t startTime = NanoTime();
	double targetTime = startTime;
	
	int64_t procTimeSum = 0;
	int procTimeSumElapsedCycles = 0;
	
	while (!shouldQuit)
	{
		int64_t beginProcTime = NanoTime();
		
		{
			std::lock_guard<std::mutex> lock(pendingInterruptsMutex);
			ioReg[IOREG_IF] |= pendingInterrupts;
			pendingInterrupts = 0;
		}
		
		int cycles = StepCPU();
		
		elapsedCycles += cycles;
		ioReg[IOREG_DIV] = elapsedCycles >> 8;
		
		mem::UpdateDMA(cycles);
		
		for (int i = 0; i < cycles; i += (cpu.doubleSpeed ? 2 : 1))
			UpdateAudio();
		
		//Updates the timer
		uint8_t tac = ioReg[IOREG_TAC];
		if (tac & 4)
		{
			if (timerOverflow)
			{
				ioReg[IOREG_IF] |= 1 << INT_TIMER;
				ioReg[IOREG_TIMA] = ioReg[IOREG_TMA];
				timerOverflow = false;
			}
			
			cyclesSinceTimerInc += cycles;
			uint32_t cyclesPerTimerInc = CYCLES_PER_TIMER_INC[tac & 3];
			while (cyclesSinceTimerInc >= cyclesPerTimerInc)
			{
				cyclesSinceTimerInc -= cyclesPerTimerInc;
				if (ioReg[IOREG_TIMA]++ == 0xFF)
					timerOverflow = true;
			}
		}
		
		targetTime += NSPerClockCycle() * cycles;
		procTimeSum += NanoTime() - beginProcTime;
		procTimeSumElapsedCycles += cycles;
		if (procTimeSumElapsedCycles >= CLOCK_RATE && DebugPane::instance)
		{
			DebugPane::instance->SetProcTimeSum(procTimeSum);
			procTimeSum = 0;
			procTimeSumElapsedCycles -= CLOCK_RATE;
		}
		
		while (NanoTime() < targetTime) { }
	}
}

int main(int argc, char** argv)
{
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS | SDL_INIT_AUDIO))
	{
		std::cerr << SDL_GetError() << "\n";
		return 1;
	}
	
	if (TTF_Init())
	{
		std::cerr << TTF_GetError() << "\n";
		return 1;
	}
	
	//Parses arguments
	const char* romPath = nullptr;
	for (int i = 1; i < argc; i++)
	{
		std::string_view arg(argv[i]);
		if (arg.size() > 2 && arg.substr(0, 2) == "-b")
		{
			AddBreakpoint(strtoll(argv[i] + 2, nullptr, 16));
		}
		if (arg == "-d")
			devMode = true;
		if (arg == "-v")
			verboseMode = true;
		if (arg == "-s")
			speedDevPrint = true;
		if (arg == "-fast")
			fastMode = true;
		
		if (argv[i][0] != '-')
			romPath = argv[i];
	}
	
	if (romPath == nullptr)
	{
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "No ROM Specified", "Expected path to ROM as command line argument.", nullptr);
		return 2;
	}
	
	//Loads the ROM
	{
		std::ifstream romStream(romPath, std::ios::binary);
		if (!romStream)
		{
			std::string msg = std::string("Failed to open file for reading: '") + romPath + "'.";
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error Opening ROM", msg.c_str(), nullptr);
			return 2;
		}
		if (!mem::Init(romStream))
		{
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Invalid ROM", "The specified ROM is not valid.", nullptr);
			return 2;
		}
	}
	
	std::string ramPath;
	if (!mem::gameName.empty())
	{
		char* prefPath = SDL_GetPrefPath("EAE", "GbEmu");
		ramPath = prefPath;
		for (char c : mem::gameName)
			ramPath += tolower(c);
		ramPath.append(".egb");
		mem::LoadRAM(ramPath);
	}
	
	constexpr int WINDOW_H = RES_Y * PIXEL_SCALE;
	const int windowWidth = RES_X * PIXEL_SCALE + (devMode ? DebugPane::WIDTH : 0);
	
	//Creates the window
	std::string windowTitle = (mem::gameName.empty() ? "EaeEmu" : mem::gameName + " - EaeEmu");
	SDL_Window* window = SDL_CreateWindow(windowTitle.c_str(), SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, windowWidth, WINDOW_H, SDL_WINDOW_SHOWN);
	if (window == nullptr)
	{
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error Creating Window", SDL_GetError(), nullptr);
		return 1;
	}
	
	SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, 0);
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	
	if (devMode)
	{
		DebugPane::instance = new DebugPane(renderer);
	}
	
	gpu::Init(renderer);
	InitCPU();
	InitInstructionDebug();
	InitInput();
	InitAudio();
	
	std::thread cpuThread(CPUThreadTarget);
	
	while (!shouldQuit)
	{
		auto startTime = std::chrono::high_resolution_clock::now();
		
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			switch (event.type)
			{
			case SDL_QUIT:
				shouldQuit = true;
				break;
			}
			
			if (DebugPane::instance)
			{
				DebugPane::instance->HandleEvent(event);
			}
			
			HandleInputEvent(event);
		}
		
		gpu::RunOneFrame();
		
		SDL_Rect copyDst = { 0, 0, RES_X * PIXEL_SCALE, RES_Y * PIXEL_SCALE };
		SDL_RenderCopy(renderer, gpu::outTexture, nullptr, &copyDst);
		
		if (DebugPane::instance)
		{
			DebugPane::instance->Draw(renderer);
		}
		
		SDL_RenderPresent(renderer);
		
		std::this_thread::sleep_until(startTime + std::chrono::nanoseconds(1000000000LL / 60));
	}
	
	cpuThread.join();
	
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
	
	if (!ramPath.empty())
		mem::SaveRAM(ramPath);
}
