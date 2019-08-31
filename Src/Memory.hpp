#pragma once

#include <cstdint>
#include <istream>
#include <mutex>

enum
{
	IOREG_JOYP = 0x00,
	IOREG_IF   = 0x0F,
	
	IOREG_LCDC = 0x40,
	IOREG_STAT = 0x41,
	IOREG_SCY  = 0x42,
	IOREG_SCX  = 0x43,
	IOREG_LY   = 0x44,
	IOREG_LYC  = 0x45,
	IOREG_WY   = 0x4A,
	IOREG_WX   = 0x4B,
	
	IOREG_DIV  = 0x04,
	IOREG_TIMA = 0x05,
	IOREG_TMA  = 0x06,
	IOREG_TAC  = 0x07,
	
	IOREG_BGP  = 0x47,
	IOREG_OBP0 = 0x48,
	IOREG_OBP1 = 0x49,
	
	IOREG_VBK  = 0x4F,
	IOREG_SVBK = 0x70,
	IOREG_KEY1 = 0x4D,
	IOREG_BGPI = 0x68,
	IOREG_BGPD = 0x69,
	IOREG_OBPI = 0x6A,
	IOREG_OBPD = 0x6B,
	
	IOREG_DMA   = 0x46,
	IOREG_HDMA1 = 0x51,
	IOREG_HDMA2 = 0x52,
	IOREG_HDMA3 = 0x53,
	IOREG_HDMA4 = 0x54,
	IOREG_HDMA5 = 0x55,
	
	IOREG_NR10 = 0x10,
	IOREG_NR11 = 0x11,
	IOREG_NR12 = 0x12,
	IOREG_NR13 = 0x13,
	IOREG_NR14 = 0x14,
	IOREG_NR21 = 0x16,
	IOREG_NR22 = 0x17,
	IOREG_NR23 = 0x18,
	IOREG_NR24 = 0x19,
	IOREG_NR30 = 0x1A,
	IOREG_NR31 = 0x1B,
	IOREG_NR32 = 0x1C,
	IOREG_NR33 = 0x1D,
	IOREG_NR34 = 0x1E,
	IOREG_NR41 = 0x20,
	IOREG_NR42 = 0x21,
	IOREG_NR43 = 0x22,
	IOREG_NR44 = 0x23,
	IOREG_NR50 = 0x24,
	IOREG_NR51 = 0x25,
	IOREG_NR52 = 0x26,
};

extern uint8_t ioReg[128];

namespace mem
{
	extern uint8_t* vramBankStart;
	
	extern uint8_t extRam[];
	extern uint8_t vram[][8 * 1024];
	extern uint8_t wram[];
	extern uint8_t oam[160];
	
	extern uint8_t backPaletteMemory[64];
	extern uint8_t spritePaletteMemory[64];
	
	extern std::mutex vramMutex;
	extern std::mutex oamMutex;
	
	extern std::string gameName;
	
	enum class MBC
	{
		MBC1,
		MBC5
	};
	
	extern MBC activeMBC;
	
	bool Init(std::istream& cartridgeStream);
	
	uint8_t Read(uint16_t address);
	
	void Write(uint16_t address, uint8_t val);
	
	void UpdateDMA(int cycles);
	
	void LoadRAM(const std::string& path);
	void SaveRAM(const std::string& path);
	
	inline uint16_t Read16(uint16_t address)
	{
		return (uint16_t)Read(address) | ((uint16_t)Read(address + 1) << 8);
	}
	
	inline void Write16(uint16_t address, uint16_t val)
	{
		Write(address, (uint8_t)(val & 0xFF));
		Write(address + 1, (uint8_t)(val >> 8));
	}
}
