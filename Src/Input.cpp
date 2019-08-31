#include "Input.hpp"
#include "CPU.hpp"
#include "Common.hpp"
#include "Memory.hpp"

#include <SDL.h>
#include <atomic>
#include <vector>
#include <iostream>

const char* BUTTON_SHORT_NAMES[8] = 
{
	"R", "L", "U", "D", "A", "B", "SEL", "ST"
};

static std::mutex buttonMaskMutex;
static uint32_t buttonDownMask;

uint32_t GetButtonMask()
{
	std::lock_guard<std::mutex> lock(buttonMaskMutex);
	return buttonDownMask;
}

static uint8_t sdlKeyToButton[SDL_NUM_SCANCODES];
static uint8_t sdlCButtonToButton[SDL_CONTROLLER_BUTTON_MAX];

struct GameController
{
	const char* name;
	SDL_GameController* controller;
};

std::vector<GameController> controllers;
SDL_GameController* activeController = nullptr;

static inline void AddGameController(SDL_GameController* controller)
{
	controllers.push_back({ SDL_GameControllerName(controller), controller });
	if (activeController == nullptr)
	{
		if (devMode)
		{
			std::cout << "Using game controller: " << controllers.back().name << std::endl;
		}
		activeController = controller;
	}
}

void InitInput()
{
	std::fill_n(sdlKeyToButton, SDL_NUM_SCANCODES, 0xFF);
	std::fill_n(sdlCButtonToButton, SDL_CONTROLLER_BUTTON_MAX, 0xFF);
	
	sdlKeyToButton[SDL_SCANCODE_LEFT]  = BTN_LEFT;
	sdlKeyToButton[SDL_SCANCODE_RIGHT] = BTN_RIGHT;
	sdlKeyToButton[SDL_SCANCODE_UP]    = BTN_UP;
	sdlKeyToButton[SDL_SCANCODE_DOWN]  = BTN_DOWN;
	sdlKeyToButton[SDL_SCANCODE_A]     = BTN_LEFT;
	sdlKeyToButton[SDL_SCANCODE_D]     = BTN_RIGHT;
	sdlKeyToButton[SDL_SCANCODE_W]     = BTN_UP;
	sdlKeyToButton[SDL_SCANCODE_S]     = BTN_DOWN;
	sdlKeyToButton[SDL_SCANCODE_Z]     = BTN_A;
	sdlKeyToButton[SDL_SCANCODE_X]     = BTN_B;
	sdlKeyToButton[SDL_SCANCODE_SPACE] = BTN_START;
	sdlKeyToButton[SDL_SCANCODE_LALT]  = BTN_SELECT;
	sdlKeyToButton[SDL_SCANCODE_RALT]  = BTN_SELECT;
	
	sdlCButtonToButton[SDL_CONTROLLER_BUTTON_DPAD_LEFT]  = BTN_LEFT;
	sdlCButtonToButton[SDL_CONTROLLER_BUTTON_DPAD_RIGHT] = BTN_RIGHT;
	sdlCButtonToButton[SDL_CONTROLLER_BUTTON_DPAD_UP]    = BTN_UP;
	sdlCButtonToButton[SDL_CONTROLLER_BUTTON_DPAD_DOWN]  = BTN_DOWN;
	sdlCButtonToButton[SDL_CONTROLLER_BUTTON_A]          = BTN_A;
	sdlCButtonToButton[SDL_CONTROLLER_BUTTON_B]          = BTN_B;
	sdlCButtonToButton[SDL_CONTROLLER_BUTTON_START]      = BTN_START;
	sdlCButtonToButton[SDL_CONTROLLER_BUTTON_GUIDE]      = BTN_SELECT;
	
	buttonDownMask = 0xFF;
	
	SDL_GameControllerEventState(SDL_ENABLE);
	SDL_GameControllerUpdate();
	SDL_JoystickEventState(SDL_ENABLE);
	SDL_JoystickUpdate();
	
	for (int i = 0; i < SDL_NumJoysticks(); i++)
	{
		if (!SDL_IsGameController(i))
		{
			if (devMode)
			{
				std::cerr << "Joystick '" << SDL_JoystickNameForIndex(i) << "' is not a game controller" << std::endl;
			}
			continue;
		}
		SDL_GameController* controller = SDL_GameControllerOpen(i);
		if (controller == nullptr)
		{
			if (devMode)
			{
				std::cerr << "Could not open game controller " << i << ": " << SDL_GetError() << std::endl;
			}
			continue;
		}
		AddGameController(controller);
	}
}

inline void SetButtonDown(uint32_t btn)
{
	if (btn != 0xFFU)
	{
		QueueInterrupt(INT_JOYPAD);
		std::lock_guard<std::mutex> lock(buttonMaskMutex);
		buttonDownMask &= ~(1U << btn);
	}
}

inline void SetButtonUp(uint32_t btn)
{
	if (btn != 0xFFU)
	{
		std::lock_guard<std::mutex> lock(buttonMaskMutex);
		buttonDownMask |= 1U << btn;
	}
}

void HandleInputEvent(SDL_Event& event)
{
	if (event.type == SDL_KEYDOWN && !event.key.repeat)
	{
		SetButtonDown(sdlKeyToButton[event.key.keysym.scancode]);
	}
	else if (event.type == SDL_KEYUP && !event.key.repeat)
	{
		SetButtonUp(sdlKeyToButton[event.key.keysym.scancode]);
	}
	else if (event.type == SDL_CONTROLLERBUTTONDOWN)
	{
		SetButtonDown(sdlCButtonToButton[event.cbutton.button]);
	}
	else if (event.type == SDL_CONTROLLERBUTTONUP)
	{
		SetButtonUp(sdlCButtonToButton[event.cbutton.button]);
	}
	else if (event.type == SDL_CONTROLLERDEVICEADDED)
	{
		AddGameController(SDL_GameControllerFromInstanceID(event.cdevice.which));
	}
}
