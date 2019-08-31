#pragma once

#include <cstdint>

enum
{
	BTN_RIGHT,
	BTN_LEFT,
	BTN_UP,
	BTN_DOWN,
	BTN_A,
	BTN_B,
	BTN_SELECT,
	BTN_START
};

extern const char* BUTTON_SHORT_NAMES[8];

uint32_t GetButtonMask();

void InitInput();
