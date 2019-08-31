#pragma once

#include <cstdint>
#include <iosfwd>

enum
{
	REG_A = 1,
	REG_F = 0,
	REG_B = 3,
	REG_C = 2,
	REG_D = 5,
	REG_E = 4,
	REG_H = 7,
	REG_L = 6,
	
	REG_AF = 0,
	REG_BC = 1,
	REG_DE = 2,
	REG_HL = 3
};

enum
{
	OP_REG_A = 0b111,
	OP_REG_B = 0b000,
	OP_REG_C = 0b001,
	OP_REG_D = 0b010,
	OP_REG_E = 0b011,
	OP_REG_H = 0b100,
	OP_REG_L = 0b101
};

extern const int OpRegToRegIdx[8];

enum
{
	FLAG_ZERO   = 7,
	FLAG_SUB    = 6,
	FLAG_HCARRY = 5,
	FLAG_CARRY  = 4
};

enum
{
	INT_VBLANK   = 0,
	INT_LCD_STAT = 1,
	INT_TIMER    = 2,
	INT_SERIAL   = 3,
	INT_JOYPAD   = 4
};

struct CPU
{
	union
	{
		uint16_t reg16[4];
		uint8_t reg8[8];
	};
	uint16_t sp;
	uint16_t pc;
	bool halted;
	bool doubleSpeed;
	
	uint8_t intEnableReg;
	bool intEnableMaster;
};

extern CPU cpu;

void InitInstructionDebug();
void PrintNextInstruction();

void AddBreakpoint(uint16_t pc);

std::ostream& operator<<(std::ostream&, const CPU& _cpu);

void InitCPU();

int StepCPU();
