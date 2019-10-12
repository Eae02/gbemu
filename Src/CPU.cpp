#include "CPU.hpp"
#include "Memory.hpp"
#include "Common.hpp"

#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <signal.h>

CPU cpu;

const int OpRegToRegIdx[8] = 
{
	REG_B,
	REG_C,
	REG_D,
	REG_E,
	REG_H,
	REG_L,
	-1,
	REG_A
};

inline uint8_t ReadPCMem()
{
	return mem::Read(cpu.pc++);
}

inline uint16_t ReadPCMem16()
{
	uint16_t val = mem::Read16(cpu.pc);
	cpu.pc += 2;
	return val;
}

enum class FlagMode
{
	Keep,
	True,
	False,
	Dep
};

inline uint8_t CarryBit()
{
	return (cpu.reg8[REG_F] >> FLAG_CARRY) & 1;
}

inline uint8_t GetAddFlags(uint8_t oldVal, uint8_t newVal)
{
	return (
		((uint8_t)(newVal == 0) << FLAG_ZERO) |
		((uint8_t)(newVal < oldVal) << FLAG_CARRY) |
		((uint8_t)((newVal & 0xF) < (oldVal & 0xF)) << FLAG_HCARRY)
	);
}

inline uint8_t GetSubFlags(uint8_t oldVal, uint8_t newVal)
{
	return (
		((uint8_t)(newVal == 0) << FLAG_ZERO) |
		((uint8_t)1 << FLAG_SUB) |
		((uint8_t)(newVal > oldVal) << FLAG_CARRY) |
		((uint8_t)((newVal & 0xF) > (oldVal & 0xF)) << FLAG_HCARRY)
	);
}

inline void DoAccAdd(uint8_t delta)
{
	uint8_t oldVal = cpu.reg8[REG_A];
	cpu.reg8[REG_A] += delta;
	cpu.reg8[REG_F] = GetAddFlags(oldVal, cpu.reg8[REG_A]);
}

inline void DoAccAdc(uint8_t delta)
{
	uint16_t c = CarryBit();
	
	uint16_t oldVal = cpu.reg8[REG_A];
	uint16_t newVal = (uint16_t)oldVal + (uint16_t)delta + c;
	cpu.reg8[REG_A] = newVal;
	
	cpu.reg8[REG_F] =
		((uint8_t)(cpu.reg8[REG_A] == 0) << FLAG_ZERO) |
		((uint8_t)(newVal > 0xFF) << FLAG_CARRY) |
		((uint8_t)((oldVal & 0xF) + (delta & 0xF) + c > 0xF) << FLAG_HCARRY);
}

inline void DoAccSub(uint8_t delta)
{
	uint8_t oldVal = cpu.reg8[REG_A];
	cpu.reg8[REG_A] -= delta;
	cpu.reg8[REG_F] = GetSubFlags(oldVal, cpu.reg8[REG_A]);
}

inline void DoAccSbc(uint8_t delta)
{
	int8_t c = CarryBit();
	
	uint8_t oldVal = cpu.reg8[REG_A];
	int deltaI = (int)delta + (int)c;
	
	cpu.reg8[REG_A] -= delta + c;
	
	cpu.reg8[REG_F] =
		((uint8_t)(cpu.reg8[REG_A] == 0) << FLAG_ZERO) |
		((uint8_t)1 << FLAG_SUB) |
		((uint8_t)((int)oldVal - deltaI < 0) << FLAG_CARRY) |
		((uint8_t)((int)(oldVal & 0xF) - (int)(delta & 0xF) - (int)c < 0) << FLAG_HCARRY);
}

template <bool SetHCarry>
inline void DoAccLogic(uint8_t newVal)
{
	cpu.reg8[REG_A] = newVal;
	cpu.reg8[REG_F] = ((uint8_t)(newVal == 0) << FLAG_ZERO) | ((uint8_t)SetHCarry << FLAG_HCARRY);
}

inline uint16_t DoAdd16(uint16_t oldVal, uint16_t delta)
{
	uint16_t newVal = oldVal + delta;
	cpu.reg8[REG_F] =
		(cpu.reg8[REG_F] & (1 << FLAG_ZERO)) |
		((uint8_t)(newVal < oldVal) << FLAG_CARRY) |
		((uint8_t)((newVal & 0x7FF) < (oldVal & 0x7FF)) << FLAG_HCARRY);
	return newVal;
}

uint16_t DoAddSP(int8_t add)
{
	cpu.reg8[REG_F] =
		(((uint32_t)(cpu.sp & 0xFF) + (uint32_t)((uint8_t)add & 0xFF) > 0xFFU) << FLAG_CARRY) | 
		(((uint32_t)(cpu.sp & 0xF) + (uint32_t)((uint8_t)add & 0xF) > 0xFU) << FLAG_HCARRY);
	return cpu.sp + add;
}

inline void UpdateFlagsAfterInc(uint8_t oldValue)
{
	cpu.reg8[REG_F] =
		((uint8_t)(oldValue == 0xFF) << FLAG_ZERO) |
		((uint8_t)((oldValue & 0xF) == 0xF) << FLAG_HCARRY) |
		(cpu.reg8[REG_F] & (1 << FLAG_CARRY));
}

inline void UpdateFlagsAfterDec(uint8_t oldValue)
{
	cpu.reg8[REG_F] =
		((uint8_t)(oldValue == 1) << FLAG_ZERO) |
		(1 << FLAG_SUB) |
		((uint8_t)((oldValue & 0xF) == 0) << FLAG_HCARRY) |
		(cpu.reg8[REG_F] & (1 << FLAG_CARRY));
}

inline void DoCall(uint16_t dst)
{
	cpu.sp -= 2;
	mem::Write16(cpu.sp, cpu.pc);
	cpu.pc = dst;
}

inline void DoRet()
{
	cpu.pc = mem::Read16(cpu.sp);
	cpu.sp += 2;
}

static const uint16_t INTERRUPT_TARGETS[] = { 0x40, 0x48, 0x50, 0x58, 0x60 };

void InitCPU()
{
	cpu.reg16[REG_AF] = 0x11B0;
	cpu.reg16[REG_BC] = 0x0013;
	cpu.reg16[REG_DE] = 0x00D8;
	cpu.reg16[REG_HL] = 0x014F;
	cpu.sp = 0xFFFE;
	cpu.pc = 0x100;
	cpu.halted = false;
	cpu.intEnableMaster = true;
	cpu.intEnableReg = 0;
}

std::vector<uint16_t> breakpoints;

void AddBreakpoint(uint16_t pc)
{
	breakpoints.push_back(pc);
}

int StepCPU()
{
	//Checks for interrupts
	if (uint32_t intFlags = cpu.intEnableReg & ioReg[IOREG_IF])
	{
		int interrupt = __builtin_ctz(intFlags);
		if (cpu.intEnableMaster)
		{
			ioReg[IOREG_IF] &= ~(1 << interrupt);
			if (verboseMode)
				std::cout << "INT " << interrupt << "\n";
			
			cpu.intEnableMaster = false;
			DoCall(INTERRUPT_TARGETS[interrupt]);
			
			if (cpu.halted)
			{
				cpu.halted = false;
				return 24;
			}
			return 20;
		}
		else if (cpu.halted)
		{
			cpu.halted = false;
			return 4;
		}
	}
	
	if (cpu.halted)
		return 4;
	
	if (verboseMode)
		PrintNextInstruction();
	
	if (std::find(breakpoints.begin(), breakpoints.end(), cpu.pc) != breakpoints.end())
	{
		std::cout << "@" << std::hex << cpu.pc << std::endl;
		//raise(SIGTRAP);
	}
	
	uint8_t instruction = ReadPCMem();
	
	switch (instruction)
	{
#define DEFINST_LD_REG_REG(x, y)\
	case (0b01000000 | ((int)(OP_REG_ ## x) << 3) | OP_REG_ ## y): \
		cpu.reg8[REG_ ## x] = cpu.reg8[REG_ ## y]; \
		return 4;
	DEFINST_LD_REG_REG(A, A)
	DEFINST_LD_REG_REG(A, B)
	DEFINST_LD_REG_REG(A, C)
	DEFINST_LD_REG_REG(A, D)
	DEFINST_LD_REG_REG(A, E)
	DEFINST_LD_REG_REG(A, H)
	DEFINST_LD_REG_REG(A, L)
	DEFINST_LD_REG_REG(B, A)
	DEFINST_LD_REG_REG(B, B)
	DEFINST_LD_REG_REG(B, C)
	DEFINST_LD_REG_REG(B, D)
	DEFINST_LD_REG_REG(B, E)
	DEFINST_LD_REG_REG(B, H)
	DEFINST_LD_REG_REG(B, L)
	DEFINST_LD_REG_REG(C, A)
	DEFINST_LD_REG_REG(C, B)
	DEFINST_LD_REG_REG(C, C)
	DEFINST_LD_REG_REG(C, D)
	DEFINST_LD_REG_REG(C, E)
	DEFINST_LD_REG_REG(C, H)
	DEFINST_LD_REG_REG(C, L)
	DEFINST_LD_REG_REG(D, A)
	DEFINST_LD_REG_REG(D, B)
	DEFINST_LD_REG_REG(D, C)
	DEFINST_LD_REG_REG(D, D)
	DEFINST_LD_REG_REG(D, E)
	DEFINST_LD_REG_REG(D, H)
	DEFINST_LD_REG_REG(D, L)
	DEFINST_LD_REG_REG(E, A)
	DEFINST_LD_REG_REG(E, B)
	DEFINST_LD_REG_REG(E, C)
	DEFINST_LD_REG_REG(E, D)
	DEFINST_LD_REG_REG(E, E)
	DEFINST_LD_REG_REG(E, H)
	DEFINST_LD_REG_REG(E, L)
	DEFINST_LD_REG_REG(H, A)
	DEFINST_LD_REG_REG(H, B)
	DEFINST_LD_REG_REG(H, C)
	DEFINST_LD_REG_REG(H, D)
	DEFINST_LD_REG_REG(H, E)
	DEFINST_LD_REG_REG(H, H)
	DEFINST_LD_REG_REG(H, L)
	DEFINST_LD_REG_REG(L, A)
	DEFINST_LD_REG_REG(L, B)
	DEFINST_LD_REG_REG(L, C)
	DEFINST_LD_REG_REG(L, D)
	DEFINST_LD_REG_REG(L, E)
	DEFINST_LD_REG_REG(L, H)
	DEFINST_LD_REG_REG(L, L)
	
#define DEFINST_LD_IMM_REG(reg)\
	case (0b00000110 | ((int)(OP_REG_ ## reg) << 3)): \
		cpu.reg8[REG_ ## reg] = ReadPCMem(); \
		return 8;
	DEFINST_LD_IMM_REG(A)
	DEFINST_LD_IMM_REG(B)
	DEFINST_LD_IMM_REG(C)
	DEFINST_LD_IMM_REG(D)
	DEFINST_LD_IMM_REG(E)
	DEFINST_LD_IMM_REG(H)
	DEFINST_LD_IMM_REG(L)
	
#define DEFINST_LD_HLMEM_REG(reg)\
	case (0b01000110 | ((int)(OP_REG_ ## reg) << 3)): \
		cpu.reg8[REG_ ## reg] = mem::Read(cpu.reg16[REG_HL]); \
		return 8;
	DEFINST_LD_HLMEM_REG(A)
	DEFINST_LD_HLMEM_REG(B)
	DEFINST_LD_HLMEM_REG(C)
	DEFINST_LD_HLMEM_REG(D)
	DEFINST_LD_HLMEM_REG(E)
	DEFINST_LD_HLMEM_REG(H)
	DEFINST_LD_HLMEM_REG(L)
	
#define DEFINST_LD_REG_HLMEM(reg)\
	case (0b01110000 | OP_REG_ ## reg): \
		mem::Write(cpu.reg16[REG_HL], cpu.reg8[REG_ ## reg]); \
		return 8;
	DEFINST_LD_REG_HLMEM(A)
	DEFINST_LD_REG_HLMEM(B)
	DEFINST_LD_REG_HLMEM(C)
	DEFINST_LD_REG_HLMEM(D)
	DEFINST_LD_REG_HLMEM(E)
	DEFINST_LD_REG_HLMEM(H)
	DEFINST_LD_REG_HLMEM(L)
	
	//Operations for loading and storing registers to memory
	case 0x36: //LD (HL) n
		mem::Write(cpu.reg16[REG_HL], ReadPCMem());
		return 12;
	case 0x0A: //LD A (BC)
		cpu.reg8[REG_A] = mem::Read(cpu.reg16[REG_BC]);
		return 8;
	case 0x1A: //LD A (DE)
		cpu.reg8[REG_A] = mem::Read(cpu.reg16[REG_DE]);
		return 8;
	case 0xFA: //LD A (nn)
		cpu.reg8[REG_A] = mem::Read(ReadPCMem16());
		return 16;
	case 0x02: //LD (BC) A
		mem::Write(cpu.reg16[REG_BC], cpu.reg8[REG_A]);
		return 8;
	case 0x12: //LD (DE) A
		mem::Write(cpu.reg16[REG_DE], cpu.reg8[REG_A]);
		return 8;
	case 0xEA: //LD (nn) A
		mem::Write(ReadPCMem16(), cpu.reg8[REG_A]);
		return 16;
	case 0x08: //LD (nn) SP
		mem::Write16(ReadPCMem16(), cpu.sp);
		return 20;
	
	//Operations for loading and storing to I/O registers
	case 0xF0: //LD A (FF00+n)
		cpu.reg8[REG_A] = mem::Read(0xFF00 + ReadPCMem());
		return 12;
	case 0xE0: //LD (FF00+n) A
		mem::Write(0xFF00 + ReadPCMem(), cpu.reg8[REG_A]);
		return 12;
	case 0xF2: //LD A (FF00+C)
		cpu.reg8[REG_A] = mem::Read(0xFF00 + cpu.reg8[REG_C]);
		return 8;
	case 0xE2: //LD (FF00+C) A
		mem::Write(0xFF00 + cpu.reg8[REG_C], cpu.reg8[REG_A]);
		return 8;
	
	//Operations for loading and storing the A register to memory at HL and incrementing/decrementing HL
	case 0x22: //LDI (HL) A
		mem::Write(cpu.reg16[REG_HL]++, cpu.reg8[REG_A]);
		return 8;
	case 0x2A: //LDI A (HL)
		cpu.reg8[REG_A] = mem::Read(cpu.reg16[REG_HL]++);
		return 8;
	case 0x32: //LDD (HL) A:
		mem::Write(cpu.reg16[REG_HL]--, cpu.reg8[REG_A]);
		return 8;
	case 0x3A: //LDD A (HL):
		cpu.reg8[REG_A] = mem::Read(cpu.reg16[REG_HL]--);
		return 8;
	
	//Operations for loading 16-bit immediate values to 16-bit registers
	case 0x01: //LD BC nn:
		cpu.reg16[REG_BC] = ReadPCMem16();
		return 12;
	case 0x11: //LD DE nn:
		cpu.reg16[REG_DE] = ReadPCMem16();
		return 12;
	case 0x21: //LD HL nn:
		cpu.reg16[REG_HL] = ReadPCMem16();
		return 12;
	case 0x31: //LD SP nn:
		cpu.sp = ReadPCMem16();
		return 12;
	case 0xF9: //LD SP HL
		cpu.sp = cpu.reg16[REG_HL];
		return 8;
	
	//Stack push instructions
	case 0xC5: //PUSH BC:
		cpu.sp -= 2;
		mem::Write16(cpu.sp, cpu.reg16[REG_BC]);
		return 16;
	case 0xD5: //PUSH DE:
		cpu.sp -= 2;
		mem::Write16(cpu.sp, cpu.reg16[REG_DE]);
		return 16;
	case 0xE5: //PUSH HL:
		cpu.sp -= 2;
		mem::Write16(cpu.sp, cpu.reg16[REG_HL]);
		return 16;
	case 0xF5: //PUSH AF:
		cpu.sp -= 2;
		mem::Write16(cpu.sp, cpu.reg16[REG_AF]);
		return 16;
	
	//Stack pop instructions
	case 0xC1: //POP BC:
		cpu.reg16[REG_BC] = mem::Read16(cpu.sp);
		cpu.sp += 2;
		return 12;
	case 0xD1: //POP DE:
		cpu.reg16[REG_DE] = mem::Read16(cpu.sp);
		cpu.sp += 2;
		return 12;
	case 0xE1: //POP HL:
		cpu.reg16[REG_HL] = mem::Read16(cpu.sp);
		cpu.sp += 2;
		return 12;
	case 0xF1: //POP AF:
		cpu.reg16[REG_AF] = mem::Read16(cpu.sp) & 0xFFF0U;
		cpu.sp += 2;
		return 12;
	
#define DEFINSTS_ARITHMETIC(reg)\
	case (0b10000000 | OP_REG_ ## reg): /* ADD A reg */ \
		DoAccAdd(cpu.reg8[REG_ ## reg]); \
		return 4; \
	case (0b10001000 | OP_REG_ ## reg): /* ADC A reg */ \
		DoAccAdc(cpu.reg8[REG_ ## reg]); \
		return 4; \
	case (0b10010000 | OP_REG_ ## reg): /* SUB A reg */ \
		DoAccSub(cpu.reg8[REG_ ## reg]); \
		return 4; \
	case (0b10011000 | OP_REG_ ## reg): /* SBC A reg */ \
		DoAccSbc(cpu.reg8[REG_ ## reg]); \
		return 4; \
	case (0b10100000 | OP_REG_ ## reg): /* AND A reg */ \
		DoAccLogic<true>(cpu.reg8[REG_A] & cpu.reg8[REG_ ## reg]); \
		return 4; \
	case (0b10101000 | OP_REG_ ## reg): /* XOR A reg */ \
		DoAccLogic<false>(cpu.reg8[REG_A] ^ cpu.reg8[REG_ ## reg]); \
		return 4; \
	case (0b10110000 | OP_REG_ ## reg): /* OR A reg */ \
		DoAccLogic<false>(cpu.reg8[REG_A] | cpu.reg8[REG_ ## reg]); \
		return 4; \
	case (0b10111000 | OP_REG_ ## reg): /* CP A reg */ \
		cpu.reg8[REG_F] = GetSubFlags(cpu.reg8[REG_A], cpu.reg8[REG_A] - cpu.reg8[REG_ ## reg]); \
		return 4; \
	case (0b00000100 | ((int)(OP_REG_ ## reg) << 3)): /* INC reg */ { \
		UpdateFlagsAfterInc(cpu.reg8[REG_ ## reg]++); \
		return 4; } \
	case (0b00000101 | ((int)(OP_REG_ ## reg) << 3)): /* DEC reg */ { \
		UpdateFlagsAfterDec(cpu.reg8[REG_ ## reg]--); \
		return 4; }
	
	DEFINSTS_ARITHMETIC(A)
	DEFINSTS_ARITHMETIC(B)
	DEFINSTS_ARITHMETIC(C)
	DEFINSTS_ARITHMETIC(D)
	DEFINSTS_ARITHMETIC(E)
	DEFINSTS_ARITHMETIC(H)
	DEFINSTS_ARITHMETIC(L)
	
	case 0xC6: //ADD A n
		DoAccAdd(ReadPCMem());
		return 8;
	case 0x86: //ADD A (HL)
		DoAccAdd(mem::Read(cpu.reg16[REG_HL]));
		return 8;
	case 0xCE: //ADC A n
		DoAccAdc(ReadPCMem());
		return 8;
	case 0x8E: //ADC A (HL)
		DoAccAdc(mem::Read(cpu.reg16[REG_HL]));
		return 8;
	case 0xD6: //SUB A n
		DoAccSub(ReadPCMem());
		return 8;
	case 0x96: //SUB A (HL)
		DoAccSub(mem::Read(cpu.reg16[REG_HL]));
		return 8;
	case 0xDE: //SBC A n
		DoAccSbc(ReadPCMem());
		return 8;
	case 0x9E: //SBC A (HL)
		DoAccSbc(mem::Read(cpu.reg16[REG_HL]));
		return 8;
	case 0xE6: //AND A n
		DoAccLogic<true>(cpu.reg8[REG_A] & ReadPCMem());
		return 8;
	case 0xA6: //AND A (HL)
		DoAccLogic<true>(cpu.reg8[REG_A] & mem::Read(cpu.reg16[REG_HL]));
		return 8;
	case 0xEE: //XOR A n
		DoAccLogic<false>(cpu.reg8[REG_A] ^ ReadPCMem());
		return 8;
	case 0xAE: //XOR A (HL)
		DoAccLogic<false>(cpu.reg8[REG_A] ^ mem::Read(cpu.reg16[REG_HL]));
		return 8;
	case 0xF6: //OR A n
		DoAccLogic<false>(cpu.reg8[REG_A] | ReadPCMem());
		return 8;
	case 0xB6: //OR A (HL)
		DoAccLogic<false>(cpu.reg8[REG_A] | mem::Read(cpu.reg16[REG_HL]));
		return 8;
		
	case 0xFE: //CP A n
		cpu.reg8[REG_F] = GetSubFlags(cpu.reg8[REG_A], cpu.reg8[REG_A] - ReadPCMem()); \
		return 8;
	case 0xBE: //CP A (HL)
		cpu.reg8[REG_F] = GetSubFlags(cpu.reg8[REG_A], cpu.reg8[REG_A] - mem::Read(cpu.reg16[REG_HL])); \
		return 8;
	
	case 0x34: //INC (HL):
	{
		uint8_t val = mem::Read(cpu.reg16[REG_HL]);
		mem::Write(cpu.reg16[REG_HL], val + 1);
		UpdateFlagsAfterInc(val);
		return 12;
	}
	case 0x35: //DEC (HL):
	{
		uint8_t val = mem::Read(cpu.reg16[REG_HL]);
		mem::Write(cpu.reg16[REG_HL], val - 1);
		UpdateFlagsAfterDec(val);
		return 12;
	}
	
	case 0x27: //DAA
	{
		int add = 0;
		bool carry = cpu.reg8[REG_F] & (1 << FLAG_CARRY);
		const bool sub = cpu.reg8[REG_F] & (1 << FLAG_SUB);
		if ((cpu.reg8[REG_F] & (1 << FLAG_HCARRY)) || (!sub && (cpu.reg8[REG_A] & 0xf) > 9)) {
			add = 6;
		}
		if (carry || (!sub && cpu.reg8[REG_A] > 0x99)) {
			add |= 0x60;
			carry = 1;
		}
		cpu.reg8[REG_A] += sub ? -add : add;
		
		cpu.reg8[REG_F] = (cpu.reg8[REG_F] & (1 << FLAG_SUB)) |
			((cpu.reg8[REG_A] == 0) << FLAG_ZERO) | ((uint8_t)carry << FLAG_CARRY);
		
		return 4;
	}
	
	case 0x2F: //CPL A
		cpu.reg8[REG_A] = ~cpu.reg8[REG_A];
		cpu.reg8[REG_F] = (cpu.reg8[REG_F] & ((1 << FLAG_ZERO) | (1 << FLAG_CARRY))) | (1 << FLAG_SUB) | (1 << FLAG_HCARRY);
		return 4;
	
	case 0x09: //ADD HL BC
		cpu.reg16[REG_HL] = DoAdd16(cpu.reg16[REG_HL], cpu.reg16[REG_BC]);
		return 8;
	case 0x19: //ADD HL DE
		cpu.reg16[REG_HL] = DoAdd16(cpu.reg16[REG_HL], cpu.reg16[REG_DE]);
		return 8;
	case 0x29: //ADD HL HL
		cpu.reg16[REG_HL] = DoAdd16(cpu.reg16[REG_HL], cpu.reg16[REG_HL]);
		return 8;
	case 0x39: //ADD HL SP
		cpu.reg16[REG_HL] = DoAdd16(cpu.reg16[REG_HL], cpu.sp);
		return 8;
	case 0x03: //INC BC
		cpu.reg16[REG_BC]++;
		return 8;
	case 0x13: //INC DE
		cpu.reg16[REG_DE]++;
		return 8;
	case 0x23: //INC HL
		cpu.reg16[REG_HL]++;
		return 8;
	case 0x33: //INC SP
		cpu.sp++;
		return 8;
	case 0x0B: //DEC BC
		cpu.reg16[REG_BC]--;
		return 8;
	case 0x1B: //DEC DE
		cpu.reg16[REG_DE]--;
		return 8;
	case 0x2B: //DEC HL
		cpu.reg16[REG_HL]--;
		return 8;
	case 0x3B: //DEC SP
		cpu.sp--;
		return 8;
	case 0xE8: //ADD SP n
	{
		cpu.sp = DoAddSP((int8_t)ReadPCMem());
		return 16;
	}
	case 0xF8: //LD HL SP+n
	{
		cpu.reg16[REG_HL] = DoAddSP((int8_t)ReadPCMem());
		return 12;
	}
	
	case 0x07: //RLCA:
	{
		uint8_t sout = cpu.reg8[REG_A] >> 7;
		cpu.reg8[REG_A] = (cpu.reg8[REG_A] << 1) | sout;
		cpu.reg8[REG_F] = sout << FLAG_CARRY;
		return 4;
	}
	case 0x17: //RLA:
	{
		uint8_t sout = cpu.reg8[REG_A] >> 7;
		cpu.reg8[REG_A] = (cpu.reg8[REG_A] << 1) | CarryBit();
		cpu.reg8[REG_F] = sout << FLAG_CARRY;
		return 4;
	}
	case 0x0F: //RRCA:
	{
		uint8_t sout = cpu.reg8[REG_A] & 1U;
		cpu.reg8[REG_A] = (cpu.reg8[REG_A] >> 1) | (sout << 7);
		cpu.reg8[REG_F] = sout << FLAG_CARRY;
		return 4;
	}
	case 0x1F: //RRA:
	{
		uint8_t sout = cpu.reg8[REG_A] & 1U;
		cpu.reg8[REG_A] = (cpu.reg8[REG_A] >> 1) | (CarryBit() << 7);
		cpu.reg8[REG_F] = sout << FLAG_CARRY;
		return 4;
	}
	case 0xCB:
	{
		auto DoRLC = [] (uint8_t val)
		{
			const uint8_t newVal = (val << 1) | (val >> 7);
			cpu.reg8[REG_F] = ((val >> 7) << FLAG_CARRY) | ((uint8_t)(newVal == 0) << FLAG_ZERO);
			return newVal;
		};
		
		auto DoRL = [] (uint8_t val)
		{
			const uint8_t newVal = (val << 1) | CarryBit();
			cpu.reg8[REG_F] = ((val >> 7) << FLAG_CARRY) | ((uint8_t)(newVal == 0) << FLAG_ZERO);
			return newVal;
		};
		
		auto DoRRC = [] (uint8_t val)
		{
			const uint8_t newVal = (val >> 1) | ((val & 1U) << 7);
			cpu.reg8[REG_F] = ((val & 1U) << FLAG_CARRY) | ((uint8_t)(newVal == 0) << FLAG_ZERO);
			return newVal;
		};
		
		auto DoRR = [] (uint8_t val)
		{
			const uint8_t newVal = (val >> 1) | (CarryBit() << 7);
			cpu.reg8[REG_F] = ((val & 1U) << FLAG_CARRY) | ((uint8_t)(newVal == 0) << FLAG_ZERO);
			return newVal;
		};
		
		auto DoSLA = [] (uint8_t val)
		{
			const uint8_t newVal = val << 1;
			cpu.reg8[REG_F] = ((val >> 7) << FLAG_CARRY) | ((uint8_t)(newVal == 0) << FLAG_ZERO);
			return newVal;
		};
		
		auto DoSRA = [] (uint8_t val)
		{
			const uint8_t newVal = (val >> 1) | (val & 0x80U);
			cpu.reg8[REG_F] = ((val & 1U) << FLAG_CARRY) | ((uint8_t)(newVal == 0) << FLAG_ZERO);
			return newVal;
		};
		
		auto DoSRL = [] (uint8_t val)
		{
			const uint8_t newVal = val >> 1;
			cpu.reg8[REG_F] = ((val & 1U) << FLAG_CARRY) | ((uint8_t)(newVal == 0) << FLAG_ZERO);
			return newVal;
		};
		
		auto DoSWAP = [] (uint8_t val)
		{
			const uint8_t newVal = ((val & 0x0FU) << 4) | ((val & 0xF0U) >> 4);
			cpu.reg8[REG_F] = ((uint8_t)(newVal == 0) << FLAG_ZERO);
			return newVal;
		};
		
		uint8_t op2 = ReadPCMem();
		if (uint8_t bitOp = (op2 & 0xC0U))
		{
			uint8_t bit = (op2 >> 3) & 7;
			auto DoTest = [&] (uint8_t val)
			{
				cpu.reg8[REG_F] = ((~(val >> bit) & 1) << FLAG_ZERO) | (1 << FLAG_HCARRY) | (cpu.reg8[REG_F] & (1 << FLAG_CARRY));
			};
			auto DoSet = [&] (uint8_t val)
			{
				return (val & ~(1 << bit)) | (1 << bit);
			};
			auto DoRes = [&] (uint8_t val)
			{
				return (val & ~(1 << bit));
			};
			
			if ((op2 & 7) == 6)
			{
				const uint16_t addr = cpu.reg16[REG_HL];
				uint8_t val = mem::Read(addr);
				if (bitOp == 0x40)
				{
					DoTest(val);
					return 12;
				}
				
				if (bitOp == 0xC0)
					val = DoSet(val);
				else // bitOp == 0x80
					val = DoRes(val);
				mem::Write(addr, val);
				return 16;
			}
			
			int reg = OpRegToRegIdx[op2 & 7];
			if (bitOp == 0x40)
				DoTest(cpu.reg8[reg]);
			else if (bitOp == 0xC0)
				cpu.reg8[reg] = DoSet(cpu.reg8[reg]);
			else // bitOp == 0x80
				cpu.reg8[reg] = DoRes(cpu.reg8[reg]);
			return 8;
		}
		
		switch (op2)
		{
#define DEFINSTS_BIT(reg) \
		case (0b00000000 | OP_REG_ ## reg): /* RLC reg */ { \
			cpu.reg8[REG_ ## reg] = DoRLC(cpu.reg8[REG_ ## reg]); \
			return 8; } \
		case (0b00010000 | OP_REG_ ## reg): /* RL reg */ { \
			cpu.reg8[REG_ ## reg] = DoRL(cpu.reg8[REG_ ## reg]); \
			return 8; } \
		case (0b00001000 | OP_REG_ ## reg): /* RRC reg */ { \
			cpu.reg8[REG_ ## reg] = DoRRC(cpu.reg8[REG_ ## reg]); \
			return 8; } \
		case (0b00011000 | OP_REG_ ## reg): /* RR reg */ { \
			cpu.reg8[REG_ ## reg] = DoRR(cpu.reg8[REG_ ## reg]); \
			return 8; } \
		case (0b00100000 | OP_REG_ ## reg): /* SLA reg */ { \
			cpu.reg8[REG_ ## reg] = DoSLA(cpu.reg8[REG_ ## reg]); \
			return 8; } \
		case (0b00101000 | OP_REG_ ## reg): /* SRA reg */ { \
			cpu.reg8[REG_ ## reg] = DoSRA(cpu.reg8[REG_ ## reg]); \
			return 8; } \
		case (0b00111000 | OP_REG_ ## reg): /* SRL reg */ { \
			cpu.reg8[REG_ ## reg] = DoSRL(cpu.reg8[REG_ ## reg]); \
			return 8; } \
		case (0b00110000 | OP_REG_ ## reg): /* SWAP reg */ { \
			cpu.reg8[REG_ ## reg] = DoSWAP(cpu.reg8[REG_ ## reg]); \
			return 8; }
			
		DEFINSTS_BIT(A)
		DEFINSTS_BIT(B)
		DEFINSTS_BIT(C)
		DEFINSTS_BIT(D)
		DEFINSTS_BIT(E)
		DEFINSTS_BIT(H)
		DEFINSTS_BIT(L)
			
		case 0x06: //RLC (HL)
		{
			const uint16_t addr = cpu.reg16[REG_HL];
			mem::Write(addr, DoRLC(mem::Read(addr)));
			return 16;
		}
		case 0x16: //RL (HL)
		{
			const uint16_t addr = cpu.reg16[REG_HL];
			mem::Write(addr, DoRL(mem::Read(addr)));
			return 16;
		}
		case 0x0E: //RRC (HL)
		{
			const uint16_t addr = cpu.reg16[REG_HL];
			mem::Write(addr, DoRRC(mem::Read(addr)));
			return 16;
		}
		case 0x1E: //RR (HL)
		{
			const uint16_t addr = cpu.reg16[REG_HL];
			mem::Write(addr, DoRR(mem::Read(addr)));
			return 16;
		}
		case 0x26: //SLA (HL)
		{
			const uint16_t addr = cpu.reg16[REG_HL];
			mem::Write(addr, DoSLA(mem::Read(addr)));
			return 16;
		}
		case 0x2E: //SRA (HL)
		{
			const uint16_t addr = cpu.reg16[REG_HL];
			mem::Write(addr, DoSRA(mem::Read(addr)));
			return 16;
		}
		case 0x3E: //SRL (HL)
		{
			const uint16_t addr = cpu.reg16[REG_HL];
			mem::Write(addr, DoSRL(mem::Read(addr)));
			return 16;
		}
		case 0x36: //SWAP (HL)
		{
			const uint16_t addr = cpu.reg16[REG_HL];
			mem::Write(addr, DoSWAP(mem::Read(addr)));
			return 16;
		}
		}
		break;
	}
	
	case 0x3F: //CCF
		cpu.reg8[REG_F] ^= 1 << FLAG_CARRY;
		cpu.reg8[REG_F] &= 0x90;
		return 4;
	case 0x37: //SCF
		cpu.reg8[REG_F] |= 1 << FLAG_CARRY;
		cpu.reg8[REG_F] &= 0x90;
		return 4;
	
	case 0x00: //NOP
		return 4;
	case 0x76: //HALT
		cpu.halted = true;
		return 4;
	case 0x10: //STOP
		if (cgbMode && ioReg[IOREG_KEY1] & 1)
		{
			cpu.doubleSpeed = !cpu.doubleSpeed;
			ioReg[IOREG_KEY1] &= 0xFE;
		}
		else
		{
			cpu.halted = true;
		}
		return 4;
	case 0xF3: //DI
		cpu.intEnableMaster = false;
		return 4;
	case 0xFB: //EI
		cpu.intEnableMaster = true;
		return 4;
	
	case 0xC3: //JP nn
		cpu.pc = mem::Read16(cpu.pc);
		return 16;
	case 0xE9: //JP HL
		cpu.pc = cpu.reg16[REG_HL];
		return 4;
	case 0xC2: //JP [NZ] nn
	{
		uint16_t jmp = ReadPCMem16();
		if (!(cpu.reg8[REG_F] & (1 << FLAG_ZERO)))
		{
			cpu.pc = jmp;
			return 16;
		}
		return 12;
	}
	case 0xCA: //JP [Z] nn
	{
		uint16_t jmp = ReadPCMem16();
		if (cpu.reg8[REG_F] & (1 << FLAG_ZERO))
		{
			cpu.pc = jmp;
			return 16;
		}
		return 12;
	}
	case 0xD2: //JP [NC] nn
	{
		uint16_t jmp = ReadPCMem16();
		if (!(cpu.reg8[REG_F] & (1 << FLAG_CARRY)))
		{
			cpu.pc = jmp;
			return 16;
		}
		return 12;
	}
	case 0xDA: //JP [C] nn
	{
		uint16_t jmp = ReadPCMem16();
		if (cpu.reg8[REG_F] & (1 << FLAG_CARRY))
		{
			cpu.pc = jmp;
			return 16;
		}
		return 12;
	}
		
	case 0x18: //JR n
		cpu.pc += (int8_t)mem::Read(cpu.pc) + 1;
		return 12;
	case 0x20: //JR [NZ] n
	{
		int8_t jmp = (int8_t)ReadPCMem();
		if (!(cpu.reg8[REG_F] & (1 << FLAG_ZERO)))
		{
			cpu.pc += jmp;
			return 12;
		}
		return 8;
	}
	case 0x28: //JR [Z] n
	{
		int8_t jmp = (int8_t)ReadPCMem();
		if (cpu.reg8[REG_F] & (1 << FLAG_ZERO))
		{
			cpu.pc += jmp;
			return 12;
		}
		return 8;
	}
	case 0x30: //JR [NC] n
	{
		int8_t jmp = (int8_t)ReadPCMem();
		if (!(cpu.reg8[REG_F] & (1 << FLAG_CARRY)))
		{
			cpu.pc += jmp;
			return 12;
		}
		return 8;
	}
	case 0x38: //JR [C] n
	{
		int8_t jmp = (int8_t)ReadPCMem();
		if (cpu.reg8[REG_F] & (1 << FLAG_CARRY))
		{
			cpu.pc += jmp;
			return 12;
		}
		return 8;
	}
	
	case 0xCD: //CALL nn
		DoCall(ReadPCMem16());
		return 24;
	case 0xC4: //CALL [NZ] nn
	{
		uint16_t jmp = ReadPCMem16();
		if (!(cpu.reg8[REG_F] & (1 << FLAG_ZERO)))
		{
			DoCall(jmp);
			return 24;
		}
		return 12;
	}
	case 0xCC: //CALL [Z] nn
	{
		uint16_t jmp = ReadPCMem16();
		if (cpu.reg8[REG_F] & (1 << FLAG_ZERO))
		{
			DoCall(jmp);
			return 24;
		}
		return 12;
	}
	case 0xD4: //CALL [NC] nn
	{
		uint16_t jmp = ReadPCMem16();
		if (!(cpu.reg8[REG_F] & (1 << FLAG_CARRY)))
		{
			DoCall(jmp);
			return 24;
		}
		return 12;
	}
	case 0xDC: //CALL [C] nn
	{
		uint16_t jmp = ReadPCMem16();
		if (cpu.reg8[REG_F] & (1 << FLAG_CARRY))
		{
			DoCall(jmp);
			return 24;
		}
		return 12;
	}
		
	case 0xC9: //RET
		DoRet();
		return 16;
	case 0xC0: //RET [NZ]
		if (!(cpu.reg8[REG_F] & (1 << FLAG_ZERO)))
		{
			DoRet();
			return 20;
		}
		return 8;
	case 0xC8: //RET [Z]
		if (cpu.reg8[REG_F] & (1 << FLAG_ZERO))
		{
			DoRet();
			return 20;
		}
		return 8;
	case 0xD0: //RET [NC]
		if (!(cpu.reg8[REG_F] & (1 << FLAG_CARRY)))
		{
			DoRet();
			return 20;
		}
		return 8;
	case 0xD8: //RET [C]
		if (cpu.reg8[REG_F] & (1 << FLAG_CARRY))
		{
			DoRet();
			return 20;
		}
		return 8;
		
	case 0xD9: //RETI
		DoRet();
		cpu.intEnableMaster = true;
		return 16;
		
	case 0xC7: //RST 00
		DoCall(0x00);
		return 16;
	case 0xD7: //RST 10
		DoCall(0x10);
		return 16;
	case 0xE7: //RST 20
		DoCall(0x20);
		return 16;
	case 0xF7: //RST 30
		DoCall(0x30);
		return 16;
	case 0xCF: //RST 08
		DoCall(0x08);
		return 16;
	case 0xDF: //RST 18
		DoCall(0x18);
		return 16;
	case 0xEF: //RST 28
		DoCall(0x28);
		return 16;
	case 0xFF: //RST 38
		DoCall(0x38);
		return 16;
	}
	
	std::cerr << "Unknown opcode " << std::hex << (int)instruction << "\n";
	std::abort();
	
	return 0;
}
