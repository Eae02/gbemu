#include "CPU.hpp"
#include "Memory.hpp"

#include <iostream>
#include <iomanip>
#include <cstring>
#include <cstdio>
#include <functional>

constexpr size_t MAX_NAME_LEN = 32;

char instructionNames[256][MAX_NAME_LEN];
std::function<void(uint16_t)> instructionPrintExtra[256];

void PrintExtraImm8(uint16_t pc)
{
	std::cout << std::setw(2) << (uint32_t)mem::Read(pc);
}

void PrintExtraRelJump(uint16_t pc)
{
	int rel = (int)(int8_t)mem::Read(pc) + 1;
	std::cout << std::dec << rel
		<< " = " << std::setw(4) << std::hex << (uint16_t)(pc + rel);
}

void PrintExtraImm16(uint16_t pc)
{
	std::cout << std::setw(4) << (uint32_t)mem::Read16(pc);
}

void PrintExtraHLMem(uint16_t pc)
{
	std::cout << "  (HL=" << std::setw(4) << cpu.reg16[REG_HL] << 
		", [HL]=" << std::setw(2) << (uint32_t)mem::Read(cpu.reg16[REG_HL]) << ")";
}

void PrintExtraHL(uint16_t pc)
{
	std::cout << "  (HL=" << std::setw(4) << cpu.reg16[REG_HL] << ")";
}

void PrintExtraHLAndA(uint16_t pc)
{
	std::cout << "  (HL=" << std::setw(4) << cpu.reg16[REG_HL] << ", A=" << std::setw(2) << (uint32_t)cpu.reg8[REG_A] << ")";
}

void PrintExtraInterruptInfo(uint16_t pc)
{
	const char* intNames[] = { "VBL", "STAT", "TIM", "SER", "JYP" };
	std::cout << "  (IME=" << cpu.intEnableMaster;
	for (int i = 0; i < 5; i++)
	{
		std::cout << ", " << intNames[i] << "=" << (int)((cpu.intEnableReg >> i) & 1);
	}
	std::cout << ")";
}

static std::pair<char, uint8_t> regs8[] = 
{
	{ 'A', OP_REG_A },
	{ 'B', OP_REG_B },
	{ 'C', OP_REG_C },
	{ 'D', OP_REG_D },
	{ 'E', OP_REG_E },
	{ 'H', OP_REG_H },
	{ 'L', OP_REG_L }
};

uint32_t stackPtrBase = 0;

void PrintExtraCB(uint16_t pc)
{
	uint8_t op = mem::Read(pc);
	
	for (auto reg : regs8)
	{
		if (op == (0b00000000 | reg.second))
		{
			std::cout << "rlc " << reg.first;
			return;
		}
		if (op == (0b00010000 | reg.second))
		{
			std::cout << "rl " << reg.first;
			return;
		}
		if (op == (0b00001000 | reg.second))
		{
			std::cout << "rrc " << reg.first;
			return;
		}
		if (op == (0b00011000 | reg.second))
		{
			std::cout << "rr " << reg.first;
			return;
		}
		if (op == (0b00100000 | reg.second))
		{
			std::cout << "sla " << reg.first;
			return;
		}
		if (op == (0b00101000 | reg.second))
		{
			std::cout << "sra " << reg.first;
			return;
		}
		if (op == (0b00111000 | reg.second))
		{
			std::cout << "srl " << reg.first;
			return;
		}
		if (op == (0b00110000 | reg.second))
		{
			std::cout << "swap " << reg.first;
			return;
		}
	}
	
	std::cout << "cb? " << std::setw(2) << (uint32_t)op;
}

std::function<void(uint16_t)> MakePrintExtraReg8(int reg)
{
	return [reg] (uint16_t pc)
	{
		std::cout << " = " << std::setw(2) << (uint32_t)cpu.reg8[reg];
	};
}

std::ostream& operator<<(std::ostream& stream, const CPU& _cpu)
{
	stream << std::hex << std::setw(2) <<
		"  A:" << (uint32_t)_cpu.reg8[REG_A] << " F:" << (uint32_t)_cpu.reg8[REG_F] << "\n"
		"  B:" << (uint32_t)_cpu.reg8[REG_B] << " C:" << (uint32_t)_cpu.reg8[REG_C] << "\n"
		"  D:" << (uint32_t)_cpu.reg8[REG_D] << " E:" << (uint32_t)_cpu.reg8[REG_C] << "\n"
		"  H:" << (uint32_t)_cpu.reg8[REG_H] << " L:" << (uint32_t)_cpu.reg8[REG_L] << "\n"
		"  SP: " << std::setw(4) << _cpu.sp << " PC: " << _cpu.pc;
	return stream;
}

void InitInstructionDebug()
{
	stackPtrBase = cpu.sp;
	
	for (size_t i = 0; i < 256; i++)
	{
		strcpy(instructionNames[i], "??");
	}
	
	auto InitInstruction = [] (uint8_t op, const char* name, std::function<void(uint16_t)> printExtra = nullptr)
	{
		strcpy(instructionNames[op], name);
		instructionPrintExtra[op] = printExtra;
	};
	
	InitInstruction(0xC5, "push BC", [] (uint16_t) { std::cout << "  (BC=" << std::setw(4) << cpu.reg16[REG_BC] << ")"; });
	InitInstruction(0xD5, "push DE", [] (uint16_t) { std::cout << "  (DE=" << std::setw(4) << cpu.reg16[REG_DE] << ")"; });
	InitInstruction(0xE5, "push HL", [] (uint16_t) { std::cout << "  (HL=" << std::setw(4) << cpu.reg16[REG_HL] << ")"; });
	InitInstruction(0xF5, "push AF", [] (uint16_t) { std::cout << "  (AF=" << std::setw(4) << cpu.reg16[REG_AF] << ")"; });
	InitInstruction(0xC1, "pop BC", [] (uint16_t) { std::cout << "  (BC=" << std::setw(4) << mem::Read16(cpu.sp) << ")"; });
	InitInstruction(0xD1, "pop DE", [] (uint16_t) { std::cout << "  (DE=" << std::setw(4) << mem::Read16(cpu.sp) << ")"; });
	InitInstruction(0xE1, "pop HL", [] (uint16_t) { std::cout << "  (HL=" << std::setw(4) << mem::Read16(cpu.sp) << ")"; });
	InitInstruction(0xF1, "pop AF", [] (uint16_t) { std::cout << "  (AF=" << std::setw(4) << mem::Read16(cpu.sp) << ")"; });
	InitInstruction(0x2F, "cpl A");
	InitInstruction(0x27, "daa");
	InitInstruction(0x07, "rlca");
	InitInstruction(0x17, "rla");
	InitInstruction(0x0F, "rrca");
	InitInstruction(0x1F, "rra");
	InitInstruction(0x3F, "ccf");
	InitInstruction(0x37, "scf");
	InitInstruction(0x00, "nop");
	InitInstruction(0x76, "halt", PrintExtraInterruptInfo);
	InitInstruction(0x10, "stop", PrintExtraInterruptInfo);
	InitInstruction(0xF3, "di");
	InitInstruction(0xFB, "ei");
	InitInstruction(0xC3, "jp ", PrintExtraImm16);
	InitInstruction(0xE9, "jp HL", PrintExtraHL);
	InitInstruction(0xC2, "jnz ", PrintExtraImm16);
	InitInstruction(0xCA, "jz ", PrintExtraImm16);
	InitInstruction(0xD2, "jnc ", PrintExtraImm16);
	InitInstruction(0xDA, "jc ", PrintExtraImm16);
	InitInstruction(0x18, "jr ", PrintExtraRelJump);
	InitInstruction(0x20, "jrnz ", PrintExtraRelJump);
	InitInstruction(0x28, "jrz ", PrintExtraRelJump);
	InitInstruction(0x30, "jrnc ", PrintExtraRelJump);
	InitInstruction(0x38, "jrc ", PrintExtraRelJump);
	InitInstruction(0xCD, "call ", PrintExtraImm16);
	InitInstruction(0xC4, "callnz ", PrintExtraImm16);
	InitInstruction(0xCC, "callz ", PrintExtraImm16);
	InitInstruction(0xD4, "callnc ", PrintExtraImm16);
	InitInstruction(0xDC, "callc ", PrintExtraImm16);
	InitInstruction(0xC9, "ret");
	InitInstruction(0xC0, "retnz");
	InitInstruction(0xC8, "retz");
	InitInstruction(0xD0, "retnc");
	InitInstruction(0xD8, "retc");
	InitInstruction(0xD9, "reti");
	InitInstruction(0xC7, "rst 00");
	InitInstruction(0xD7, "rst 10");
	InitInstruction(0xE7, "rst 20");
	InitInstruction(0xF7, "rst 30");
	InitInstruction(0xCF, "rst 08");
	InitInstruction(0xDF, "rst 18");
	InitInstruction(0xEF, "rst 28");
	InitInstruction(0xFF, "rst 38");
	
	InitInstruction(0x22, "ldi [HL] <- A; inc HL", PrintExtraHLAndA);
	InitInstruction(0x2A, "ldi A <- [HL]; inc HL", PrintExtraHLMem);
	InitInstruction(0x32, "ldd [HL] <- A; dec HL", PrintExtraHLAndA);
	InitInstruction(0x3A, "ldd A <- [HL]; dec HL", PrintExtraHLMem);
	InitInstruction(0x01, "ld BC <- ", PrintExtraImm16);
	InitInstruction(0x11, "ld DE <- ", PrintExtraImm16);
	InitInstruction(0x21, "ld HL <- ", PrintExtraImm16);
	InitInstruction(0x31, "ld SP <- ", PrintExtraImm16);
	InitInstruction(0xF9, "ld SP <- HL");
	
	InitInstruction(0x36, "ld [HL] <- ", PrintExtraImm8);
	InitInstruction(0x0A, "ld A <- [BC]");
	InitInstruction(0x1A, "ld A <- [DE]");
	InitInstruction(0xFA, "ld A <- [nn]  nn=", PrintExtraImm16);
	InitInstruction(0x02, "ld [BC] <- A", MakePrintExtraReg8(REG_A));
	InitInstruction(0x12, "ld [DE] <- A", MakePrintExtraReg8(REG_A));
	InitInstruction(0xEA, "ld [nn] <- A  nn=", PrintExtraImm16);
	
	InitInstruction(0xF0, "ld ", [] (uint16_t pc) {
		uint32_t addr = 0xFF00 + (uint32_t)mem::Read(pc);
		std::cout << std::setw(2) << "A <- [" << addr << "] = " << (uint32_t)mem::Read(addr);
	});
	InitInstruction(0xE0, "ld ", [] (uint16_t pc) {
		uint32_t addr = 0xFF00 + (uint32_t)mem::Read(pc);
		std::cout << "[" << addr << "] <- A = " << std::setw(2) << (uint32_t)cpu.reg8[REG_A];
	});
	InitInstruction(0xF2, "ld A <- [FF00+C]", [] (uint16_t pc) {
		uint32_t addr = 0xFF00 + (uint32_t)cpu.reg8[REG_C];
		std::cout << std::setw(2) << " = [" << addr << "] = " << (uint32_t)mem::Read(addr);
	});
	InitInstruction(0xE2, "ld [FF00+C] <- A", [] (uint16_t pc) {
		std::cout << std::setw(2) << " (C:" << (uint32_t)cpu.reg8[REG_C] << ")";
	});
	
	InitInstruction(0xFE, "cp A ", PrintExtraImm8);
	InitInstruction(0xBE, "cp A [HL]", PrintExtraHLMem);
	
	InitInstruction(0xCB, "", PrintExtraCB);
	
	InitInstruction(0xE6, "A <- A & ", PrintExtraImm8);
	
	//ld reg <- reg
	for (auto r1 : regs8)
	{
		for (auto r2 : regs8)
		{
			uint32_t op = 0b01000000 | (r1.second << 3) | r2.second;
			snprintf(instructionNames[op], MAX_NAME_LEN, "ld %c <- %c", r1.first, r2.first);
			instructionPrintExtra[op] = MakePrintExtraReg8(OpRegToRegIdx[r2.second]);
		}
	}
	
	//ld reg <- imm
	for (auto r : regs8)
	{
		uint32_t op = 0b00000110 | (r.second << 3);
		snprintf(instructionNames[op], MAX_NAME_LEN, "ld %c <- ", r.first);
		instructionPrintExtra[op] = PrintExtraImm8;
	}
	
	//ld reg <- [HL]
	for (auto r : regs8)
	{
		uint32_t op = 0b01000110 | (r.second << 3);
		snprintf(instructionNames[op], MAX_NAME_LEN, "ld %c <- [HL]", r.first);
		instructionPrintExtra[op] = PrintExtraHLMem;
	}
	
	//ld [HL] <- reg
	for (auto r : regs8)
	{
		uint32_t op = 0b01110000 | r.second;
		snprintf(instructionNames[op], MAX_NAME_LEN, "ld [HL] <- %c", r.first);
		
		instructionPrintExtra[op] = [r] (uint16_t)
		{
			std::cout << "  (" << r.first << "=" << std::setw(2) << (uint32_t)cpu.reg8[OpRegToRegIdx[r.second]] <<
				", HL=" << std::setw(4) << cpu.reg16[REG_HL] << ")";
		};
	}
	
	//alu instructions
	for (auto r : regs8)
	{
		uint32_t regIdx = OpRegToRegIdx[r.second];
		
		uint8_t cpOp = 0b10111000 | r.second;
		snprintf(instructionNames[cpOp], MAX_NAME_LEN, "cp A %c", r.first);
		instructionPrintExtra[cpOp] = [=] (uint16_t) {
			std::cout << "  (A=" << std::setw(2) << +cpu.reg8[REG_A] << ", " << r.first << "=" << +cpu.reg8[regIdx] << ")";
		};
		
		uint8_t addOp = 0b10000000 | r.second;
		snprintf(instructionNames[addOp], MAX_NAME_LEN, "A <- A + %c", r.first);
		instructionPrintExtra[addOp] = [=] (uint16_t) {
			uint32_t result = cpu.reg8[REG_A] + cpu.reg8[regIdx];
			std::cout << "  = " << std::setw(2) << +cpu.reg8[REG_A] << "+" << +cpu.reg8[regIdx] << " = " << result;
		};
		
		uint8_t adcOp = 0b10001000 | r.second;
		snprintf(instructionNames[adcOp], MAX_NAME_LEN, "A <- A + %c + CF", r.first);
		instructionPrintExtra[adcOp] = [=] (uint16_t) {
			uint32_t carry = (cpu.reg8[REG_F] >> FLAG_CARRY) & 1;
			uint32_t result = cpu.reg8[REG_A] + cpu.reg8[regIdx] + carry;
			std::cout << "  = " << std::setw(2) << +cpu.reg8[REG_A] << "+" << +cpu.reg8[regIdx] << "+" << ('0' + carry) << " = " << result;
		};
		
		uint8_t subOp = 0b10010000 | r.second;
		snprintf(instructionNames[subOp], MAX_NAME_LEN, "A <- A - %c", r.first);
		instructionPrintExtra[subOp] = [=] (uint16_t) {
			uint32_t result = cpu.reg8[REG_A] - cpu.reg8[regIdx];
			std::cout << "  = " << std::setw(2) << +cpu.reg8[REG_A] << "-" << +cpu.reg8[regIdx] << " = " << result;
		};
		
		uint8_t sbcOp = 0b10011000 | r.second;
		snprintf(instructionNames[sbcOp], MAX_NAME_LEN, "A <- A - %c - CF", r.first);
		instructionPrintExtra[sbcOp] = [=] (uint16_t) {
			uint32_t carry = (cpu.reg8[REG_F] >> FLAG_CARRY) & 1;
			uint32_t result = cpu.reg8[REG_A] - cpu.reg8[regIdx] - carry;
			std::cout << "  = " << std::setw(2) << +cpu.reg8[REG_A] << "-" << +cpu.reg8[regIdx] << "-" << ('0' + carry) << " = " << result;
		};
		
		uint8_t andOp = 0b10100000 | r.second;
		snprintf(instructionNames[andOp], MAX_NAME_LEN, "A <- A & %c", r.first);
		instructionPrintExtra[andOp] = [=] (uint16_t) {
			uint32_t result = cpu.reg8[REG_A] & cpu.reg8[regIdx];
			std::cout << "  = " << std::setw(2) << +cpu.reg8[REG_A] << "&" << +cpu.reg8[regIdx] << " = " << result;
		};
		
		uint8_t xorOp = 0b10101000 | r.second;
		snprintf(instructionNames[xorOp], MAX_NAME_LEN, "A <- A ^ %c", r.first);
		instructionPrintExtra[xorOp] = [=] (uint16_t) {
			uint32_t result = cpu.reg8[REG_A] ^ cpu.reg8[regIdx];
			std::cout << "  = " << std::setw(2) << +cpu.reg8[REG_A] << "^" << +cpu.reg8[regIdx] << " = " << +result;
		};
		
		uint8_t orOp = 0b10110000 | r.second;
		snprintf(instructionNames[orOp], MAX_NAME_LEN, "A <- A | %c", r.first);
		instructionPrintExtra[orOp] = [=] (uint16_t) {
			uint32_t result = cpu.reg8[REG_A] | cpu.reg8[regIdx];
			std::cout << "  = " << std::setw(2) << +cpu.reg8[REG_A] << "|" << +cpu.reg8[regIdx] << " = " << +result;
		};
		
		
		uint8_t incOp = 0b00000100 | (r.second << 3);
		snprintf(instructionNames[incOp], MAX_NAME_LEN, "inc %c", r.first);
		instructionPrintExtra[incOp] = [r] (uint16_t)
		{
			std::cout << " (" << r.first << "=" << std::setw(2) << (uint32_t)(cpu.reg8[OpRegToRegIdx[r.second]] + 1) << ")";
		};
		
		uint8_t decOp = 0b00000101 | (r.second << 3);
		snprintf(instructionNames[decOp], MAX_NAME_LEN, "dec %c", r.first);
		instructionPrintExtra[decOp] = [r] (uint16_t)
		{
			std::cout << " (" << r.first << "=" << std::setw(2) << (uint32_t)(cpu.reg8[OpRegToRegIdx[r.second]] - 1) << ")";
		};
	}
}

bool changeStack = false;

void PrintNextInstruction()
{
	if (changeStack)
		stackPtrBase = cpu.sp;
	
	int depth = std::max(std::min((int)stackPtrBase - (int)cpu.sp, 100), 0);
	
	for (int i = 0; i < depth; i++)
		std::cout << " ";
	
	uint8_t op = mem::Read(cpu.pc);
	changeStack = op == 0x31 || op == 0xF9 || op == 0xE8;
	
	std::cout << std::setfill('0') << std::hex << "[" << std::setw(2) << (uint32_t)op << " @ " << std::setw(4) << cpu.pc << "] " << instructionNames[op];
	
	if (instructionPrintExtra[op])
		instructionPrintExtra[op](cpu.pc + 1);
	
	std::cout << std::setw(0) << std::endl;
}
