#pragma once

#include <cstdint>
#include <mutex>

#include "Common.hpp"

constexpr int RES_X = 160;
constexpr int RES_Y = 144;
constexpr int PIXEL_SCALE = 4;

struct SDL_Texture;
struct SDL_Renderer;

static constexpr uint8_t SPF_CGB_VRAM_BANK = 0x8;
static constexpr uint8_t SPF_PALETTE1 = 0x10;
static constexpr uint8_t SPF_FLIP_X = 0x20;
static constexpr uint8_t SPF_FLIP_Y = 0x40;
static constexpr uint8_t SPF_BACKGROUND = 0x80;

namespace gpu
{
	struct RegisterState
	{
		uint8_t ly;
		uint8_t lyc;
		uint8_t lcdc;
		uint8_t stat;
		uint8_t scx;
		uint8_t scy;
		uint8_t wx;
		uint8_t wy;
		uint8_t bgp;
		uint8_t obp0;
		uint8_t obp1;
	};
	
	extern std::mutex regMutex;
	extern RegisterState reg;
	
	extern SDL_Texture* outTexture;
	
	extern uint8_t prevOAM[160];
	
	struct Tile
	{
		uint16_t rows[8];
		
		inline uint8_t At(uint32_t x, uint32_t y) const
		{
			uint16_t rowSh = (rows[y] >> (7 - x));
			return ((rowSh & 1)) | (((rowSh >> 8) & 1) << 1);
		}
	};
	
	struct CGBPalette
	{
		uint16_t colors[4];
	};
	
	extern const uint16_t MONOCHROME_COLORS[4];
	
	inline uint32_t ToColor32(uint16_t color16)
	{
		uint32_t color32 = 0xFF;
		for (uint32_t c = 0; c < 3; c++)
		{
			color32 |= ((((color16 >> (c * 5)) & 0x1FU) + 1) * 8 - 1) << (24 - c * 8);
		}
		return color32;
	}
	
	inline uint16_t ResolveColorMonochrome(uint8_t colorIdx, uint8_t palette)
	{
		return MONOCHROME_COLORS[(palette >> (colorIdx * 2)) & 3];
	}
	
	void Init(SDL_Renderer* renderer);
	
	uint8_t GetRegisterSTAT();
	
	void RunOneFrame();
}
