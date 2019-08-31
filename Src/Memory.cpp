#include "Memory.hpp"
#include "CPU.hpp"
#include "GPU.hpp"
#include "Input.hpp"
#include "Common.hpp"
#include "Audio.hpp"

#include <cstring>
#include <vector>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <cassert>

#define ZLIB_CONST
#include <zlib.h>

uint8_t ioReg[128];

namespace mem
{
	static std::vector<uint8_t> cartridgeData;
	
	uint8_t* romBankStart;    //Start of switchable ROM bank at 0x4000
	uint8_t* extRamBankStart; //Start of external RAM bank at 0xA000
	uint8_t* vramBankStart;   //Start of VRAM bank at 0x8000
	uint8_t* wramBankStart;   //Start of switchable WRAM bank at 0xD000
	
	std::mutex vramMutex;
	std::mutex oamMutex;
	
	uint8_t extRam[256 * 1024];
	uint8_t vram[2][8 * 1024];
	uint8_t wram[32 * 1024];
	uint8_t oam[160];
	
	uint8_t backPaletteMemory[64];
	uint8_t spritePaletteMemory[64];
	
	static uint8_t ioRegReadMask[128];
	
	static uint8_t hram[127];
	
	enum class BankMode
	{
		ROM,
		RAM
	};
	
	MBC activeMBC;
	
	static BankMode bankMode;
	static uint32_t currentRomBank;
	
	void UpdateCurrentRomBank()
	{
		uint32_t bankIdx = currentRomBank;
		if ((bankIdx % 32) == 0 && activeMBC == MBC::MBC1)
			bankIdx++;
		romBankStart = cartridgeData.data() + (16 * 1024) * bankIdx;
	}
	
	std::string gameName;
	static bool canSave = false;
	
	bool Init(std::istream& cartridgeStream)
	{
		cartridgeData.clear();
		char cartReadBuf[4096];
		while (!cartridgeStream.eof())
		{
			cartridgeStream.read(cartReadBuf, sizeof(cartReadBuf));
			cartridgeData.insert(cartridgeData.end(), cartReadBuf, cartReadBuf + cartridgeStream.gcount());
		}
		
		if (cartridgeData.size() <= 0x014F)
			return false;
		
		uint8_t mbcMode = cartridgeData[0x147];
		switch (mbcMode)
		{
		case 0x00:
		case 0x01:
		case 0x02:
			activeMBC = MBC::MBC1;
			break;
		case 0x03:
			canSave = true;
			activeMBC = MBC::MBC1;
			break;
		case 0x05:
			activeMBC = MBC::MBC2;
			break;
		case 0x06:
			canSave = true;
			activeMBC = MBC::MBC2;
			break;
		case 0x19:
		case 0x1A:
		case 0x1C:
		case 0x1D:
			activeMBC = MBC::MBC5;
			break;
		case 0x1B:
		case 0x1E:
			canSave = true;
			activeMBC = MBC::MBC5;
			break;
		default:
			std::cerr << "Unknown MBC: " << std::hex << std::setw(2) << (uint32_t)mbcMode << "\n";
			return false;
		}
		
		const char* titleBegin = (char*)cartridgeData.data() + 0x134;
		size_t titleLen = 15;
		for (size_t i = 0; i < 15; i++)
		{
			if (titleBegin[i] == '\0')
			{
				titleLen = i;
				break;
			}
		}
		gameName = std::string(titleBegin, titleLen);
		for (int i = 0; i < gameName.size(); i++)
		{
			if (i != 0 && gameName[i - 1] != ' ')
				gameName[i] = std::tolower(gameName[i]);
		}
		
		
		bankMode = BankMode::ROM;
		currentRomBank = 1;
		UpdateCurrentRomBank();
		
		cgbMode = cartridgeData[0x143] == 0x80 || cartridgeData[0x143] == 0xC0;
		extRamBankStart = extRam;
		vramBankStart = vram[0];
		wramBankStart = wram + 4 * 1024;
		
		memset(ioRegReadMask, 0xFF, sizeof(ioRegReadMask));
		
		memset(ioReg, 0, sizeof(ioReg));
		ioReg[IOREG_NR10] = 0x80;
		ioReg[IOREG_NR11] = 0xBF;
		ioReg[IOREG_NR12] = 0xF3;
		ioReg[IOREG_NR14] = 0xBF;
		ioReg[IOREG_NR21] = 0x3F;
		ioReg[IOREG_NR24] = 0xBF;
		ioReg[IOREG_NR30] = 0x7F;
		ioReg[IOREG_NR31] = 0xFF;
		ioReg[IOREG_NR32] = 0x9F;
		ioReg[IOREG_NR33] = 0xBF;
		ioReg[IOREG_NR41] = 0xFF;
		ioReg[IOREG_NR44] = 0xBF;
		ioReg[IOREG_NR50] = 0x77;
		ioReg[IOREG_NR51] = 0xF3;
		ioReg[IOREG_NR52] = 0xF1;
		ioReg[IOREG_LCDC] = 0x91;
		ioReg[IOREG_BGP]  = 0xFC;
		
		return true;
	}
	
	inline uint8_t* ResolveAddress(uint16_t address)
	{
		switch (address)
		{
		case 0x0000 ... 0x3FFF:
			return &cartridgeData[address - 0x0000];
		case 0x4000 ... 0x7FFF:
			return &romBankStart[address - 0x4000];
		case 0x8000 ... 0x9FFF:
			return &vramBankStart[address - 0x8000];
		case 0xA000 ... 0xBFFF:
			return &extRamBankStart[address - 0xA000];
		case 0xC000 ... 0xCFFF:
			return &wram[address - 0xC000];
		case 0xD000 ... 0xDFFF:
			return &wramBankStart[address - 0xD000];
		case 0xE000 ... 0xFDFF:
			return &wram[address - 0xE000];
		case 0xFE00 ... 0xFE9F:
			return &oam[address - 0xFE00];
		case 0xFF80 ... 0xFFFE:
			return &hram[address - 0xFF80];
		case 0xFF00 ... 0xFF7F:
			return &ioReg[address - 0xFF00];
		default:
			return nullptr;
		}
	}
	
	uint8_t Read(uint16_t address)
	{
		switch (address)
		{
		case 0xFF00 | IOREG_JOYP:
		{
			uint8_t val = ioReg[IOREG_JOYP] & 0x30;
			if (val & (1 << 5))
				val |= GetButtonMask() & 0xF;
			else if (val & (1 << 4))
				val |= (GetButtonMask() >> 4) & 0xF;
			return val;
		}
		
		case 0xFF00 | IOREG_KEY1:
			return ioReg[IOREG_KEY1] | (cpu.doubleSpeed << 7);
		
		case 0xFF00 | IOREG_LY:
		{
			std::lock_guard<std::mutex> lock(gpu::regMutex);
			return gpu::reg.ly;
		}
		
		case 0xFF00 | IOREG_STAT:
			return gpu::GetRegisterSTAT();
		
		case 0xFF00 | IOREG_BGPD:
			return backPaletteMemory[ioReg[IOREG_BGPI] & 0x3F];
			break;
		case 0xFF00 | IOREG_OBPD:
			return spritePaletteMemory[ioReg[IOREG_OBPI] & 0x3F];
			break;
		
		case 0xFFFF:
			return cpu.intEnableReg;
		default:
			if (uint8_t* ptr = ResolveAddress(address))
			{
				return *ptr;
			}
			return 0;
		}
	}
	
	static int dmaMin = -1;
	static int dmaProgress = 0;
	
	void Write(uint16_t address, uint8_t val)
	{
		switch (address)
		{
		case 0x0000 ... 0x1FFF:
			break;
			
		case 0x2000 ... 0x2FFF:
			if (activeMBC == MBC::MBC1 || activeMBC == MBC::MBC2)
				currentRomBank = (currentRomBank & ~0x1FU) | (val & 0x1FU);
			else if (activeMBC == MBC::MBC5)
				currentRomBank = (currentRomBank & ~0xFFU) | val;
			UpdateCurrentRomBank();
			break;
		
		case 0x3000 ... 0x3FFF:
			if (activeMBC == MBC::MBC1 || activeMBC == MBC::MBC2)
				currentRomBank = (currentRomBank & ~0x1FU) | (val & 0x1FU);
			else if (activeMBC == MBC::MBC5)
				currentRomBank = (currentRomBank & ~(uint32_t)(1 << 8)) | (((uint32_t)val & 1) << 8);
			UpdateCurrentRomBank();
			break;
		
		case 0x4000 ... 0x5FFF:
			if (activeMBC == MBC::MBC5 || bankMode == BankMode::RAM)
			{
				extRamBankStart = extRam + (1024 * 8) * (size_t)std::max((int)val, 1);
			}
			else if (bankMode == BankMode::ROM)
			{
				currentRomBank = (currentRomBank & ~(0b11U << 5)) | ((val & 0b11) << 5);
				UpdateCurrentRomBank();
			}
			break;
		
		case 0x6000 ... 0x7FFF:
			bankMode = (BankMode)val;
			break;
			
		case 0x8000 ... 0x9FFF:
		{
			std::lock_guard<std::mutex> lock(vramMutex);
			vramBankStart[address - 0x8000] = val;
			break;
		}
		case 0xFE00 ... 0xFE9F:
		{
			std::lock_guard<std::mutex> lock(oamMutex);
			oam[address - 0xFE00] = val;
			break;
		}
		
		case 0xFF00 | IOREG_DIV:
			ioReg[IOREG_DIV] = 0; //Writing any value to this should reset it to 0
			break;
		case 0xFF00 | IOREG_VBK:
			vramBankStart = vram[val];
			ioReg[IOREG_VBK] = val;
			break;
		case 0xFF00 | IOREG_SVBK:
			wramBankStart = wram + (4 * 1024) * std::max(val & 7, 1);
			ioReg[IOREG_SVBK] = val & 7;
			break;
		case 0xFF00 | IOREG_DMA:
			dmaMin = (uint16_t)val * 0x100U;
			dmaProgress = 0;
			break;
			
		case 0xFF00 | IOREG_KEY1:
			ioReg[IOREG_KEY1] = val & 1;
			break;
			
		case 0xFF00 | IOREG_BGPD:
		{
			uint32_t bgpi = ioReg[IOREG_BGPI];
			uint32_t idx = bgpi & 0x3F;
			if (bgpi & 0x80)
				ioReg[IOREG_BGPI] = ((idx + 1) & 0x3F) | 0x80;
			
			std::lock_guard<std::mutex> lock(vramMutex);
			backPaletteMemory[idx] = val;
			break;
		}
		case 0xFF00 | IOREG_OBPD:
		{
			uint32_t obpi = ioReg[IOREG_OBPI];
			uint32_t idx = obpi & 0x3F;
			if (obpi & 0x80)
				ioReg[IOREG_OBPI] = ((idx + 1) & 0x3F) | 0x80;
			
			std::lock_guard<std::mutex> lock(vramMutex);
			spritePaletteMemory[idx] = val;
			break;
		}
		
		case 0xFF00 | IOREG_HDMA1:
		case 0xFF00 | IOREG_HDMA2:
		case 0xFF00 | IOREG_HDMA3:
		case 0xFF00 | IOREG_HDMA4:
		case 0xFF00 | IOREG_HDMA5:
			std::abort();
		
		#define DEF_WRITE_GPU_REGISTER(name, field) \
		case 0xFF00 | name: { ioReg[name] = val; std::lock_guard<std::mutex> lock(gpu::regMutex); gpu::reg.field = val; break; }
		DEF_WRITE_GPU_REGISTER(IOREG_LYC, lyc)
		DEF_WRITE_GPU_REGISTER(IOREG_LCDC, lcdc)
		DEF_WRITE_GPU_REGISTER(IOREG_STAT, stat)
		DEF_WRITE_GPU_REGISTER(IOREG_SCX, scx)
		DEF_WRITE_GPU_REGISTER(IOREG_SCY, scy)
		DEF_WRITE_GPU_REGISTER(IOREG_WX, wx)
		DEF_WRITE_GPU_REGISTER(IOREG_WY, wy)
		DEF_WRITE_GPU_REGISTER(IOREG_BGP, bgp)
		DEF_WRITE_GPU_REGISTER(IOREG_OBP0, obp0)
		DEF_WRITE_GPU_REGISTER(IOREG_OBP1, obp1)
		
		case 0xFF00 | IOREG_NR14:
		{
			ioReg[IOREG_NR14] = val;
			if (val & 1 << 7)
				ResetAudioChannel(1);
			break;
		}
		
		case 0xFF00 | IOREG_NR24:
		{
			ioReg[IOREG_NR24] = val;
			if (val & 1 << 7)
				ResetAudioChannel(2);
			break;
		}
		
		case 0xFF00 | IOREG_NR34:
		{
			ioReg[IOREG_NR34] = val;
			if (val & 1 << 7)
				ResetAudioChannel(3);
			break;
		}
		
		case 0xFF00 | IOREG_LY: break;
			
		case 0xFFFF:
			cpu.intEnableReg = val;
			break;
		default:
			if (uint8_t* ptr = ResolveAddress(address))
			{
				*ptr = val;
			}
			else if (!(address >= 0xFEA0 && address <= 0xFEFF))
			{
				std::cerr << "Write to invalid address " << std::setw(4) << address << "\n";
				std::abort();
			}
			break;
		}
	}
	
	void UpdateDMA(int cycles)
	{
		if (dmaMin == -1)
			return;
		
		std::lock_guard<std::mutex> lock(oamMutex);
		
		cycles = std::min<int>(cycles, (int)sizeof(oam) - (int)dmaProgress);
		
		while (cycles > 0)
		{
			oam[dmaProgress] = *ResolveAddress(dmaMin + dmaProgress);
			dmaProgress++;
			cycles--;
		}
		
		if (dmaProgress == sizeof(oam))
		{
			dmaMin = -1;
		}
	}
	
	static constexpr char MAGIC[] = { (char)0xFF, 'E', 'G', 'B' };
	
	void LoadRAM(const std::string& path)
	{
		if (!canSave)
			return;
		
		std::ifstream stream(path, std::ios::binary);
		if (!stream)
			return;
		
		char magicBuf[4];
		stream.read(magicBuf, sizeof(magicBuf));
		if (!std::equal(magicBuf, magicBuf + 4, MAGIC))
		{
			std::cerr << "Invalid RAM save" << std::endl;
			return;
		}
		
		z_stream inflateStream = { };
		inflateStream.avail_out = sizeof(extRam);
		inflateStream.next_out = extRam;
		if (inflateInit(&inflateStream) != Z_OK)
		{
			std::cerr << "Error initializing ZLIB" << std::endl;
			return;
		}
		
		int status = 0;
		std::array<char, 1024> buffer;
		
		while (!stream.eof() && status != Z_STREAM_END)
		{
			stream.read(buffer.data(), buffer.size());
			
			inflateStream.avail_in = stream.gcount();
			inflateStream.next_in = reinterpret_cast<Bytef*>(buffer.data());
			
			status = inflate(&inflateStream, Z_NO_FLUSH);
			assert(status != Z_STREAM_ERROR);
			
			if (status == Z_MEM_ERROR || status == Z_DATA_ERROR || status == Z_NEED_DICT)
			{
				std::cerr << "ZLIB Error " << status << std::endl;
				std::memset(extRam, 0, sizeof(extRam));
				return;
			}
		}
		
		inflateEnd(&inflateStream);
	}
	
	void SaveRAM(const std::string& path)
	{
		if (!canSave)
			return;
		
		z_stream deflateStream = { };
		deflateStream.avail_in = sizeof(extRam);
		deflateStream.next_in = extRam;
		if (deflateInit(&deflateStream, Z_DEFAULT_COMPRESSION) != Z_OK)
		{
			std::cerr << "Error initializing ZLIB\n";
			return;
		}
		
		std::ofstream stream(path, std::ios::binary);
		stream.write(MAGIC, sizeof(MAGIC));
		
		while (deflateStream.avail_in > 0)
		{
			std::array<char, 1024> outBuffer;
			
			deflateStream.avail_out = outBuffer.size();
			deflateStream.next_out = reinterpret_cast<Bytef*>(outBuffer.data());
			
			int status = deflate(&deflateStream, Z_FINISH);
			assert(status != Z_STREAM_ERROR);
			
			stream.write(outBuffer.data(), outBuffer.size() - deflateStream.avail_out);
		}
		
		deflateEnd(&deflateStream);
	}
}
