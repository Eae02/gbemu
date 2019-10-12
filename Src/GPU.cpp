#include "GPU.hpp"
#include "Memory.hpp"
#include "Common.hpp"
#include "CPU.hpp"

#include <SDL.h>
#include <bitset>
#include <mutex>
#include <thread>
#include <algorithm>

std::mutex gpu::regMutex;
gpu::RegisterState gpu::reg;

static uint8_t gpuMode;

SDL_Texture* gpu::outTexture;

void gpu::Init(SDL_Renderer* renderer)
{
	reg = { };
	reg.lcdc = 0x91;
	reg.bgp = 0xFC;
	gpuMode = 1;
	outTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, RES_X, RES_Y);
}

uint8_t gpu::GetRegisterSTAT()
{
	std::lock_guard<std::mutex> lock(regMutex);
	return (reg.stat & 0xF8U) | ((reg.lyc == reg.ly) << 2) | gpuMode;
}

inline static void SetGPUMode(int8_t mode, int8_t ly)
{
	std::lock_guard<std::mutex> lock(gpu::regMutex);
	gpuMode = mode;
	gpu::reg.ly = ly;
}

// Mode 0 = HBlank
// Mode 1 = VBlank
// Mode 2 = Reading OAM
// Mode 3 = Reading OAM & VRAM

static constexpr int64_t MODE_2_END_NS = 19000;
static constexpr int64_t MODE_3_END_NS = 40000;
static constexpr int64_t MODE_0_END_NS = 80000;
static constexpr int64_t MODE_0_MIN_NS = 30000;

struct Sprite
{
	int x;
	uint8_t tile;
	uint8_t row;
	uint8_t flags;
	uint8_t palette;
};

inline uint16_t ResolveCGBColor(uint8_t* paletteMem, uint32_t paletteIdx, uint8_t color)
{
	return reinterpret_cast<const uint16_t*>(paletteMem)[paletteIdx * 4 + color];
}

inline std::pair<bool, uint16_t> SampleSprite(const Sprite& sprite, int x)
{
	int vramBank = ((bool)sprite.flags & SPF_CGB_VRAM_BANK) && cgbMode;
	
	gpu::Tile* tileMem = reinterpret_cast<gpu::Tile*>(mem::vram[vramBank]);
	
	int srcX = (sprite.flags & SPF_FLIP_X) ? (7 - x) : x;
	uint8_t color = tileMem[sprite.tile].At(srcX, sprite.row);
	uint16_t colorRes;
	if (cgbMode)
	{
		colorRes = ResolveCGBColor(mem::spritePaletteMemory, sprite.flags & 7, color);
	}
	else
	{
		colorRes = gpu::ResolveColorMonochrome(color, sprite.palette);
	}
	return std::make_pair(color == 0, colorRes);
}

uint8_t gpu::prevOAM[160];

//Tracks the screen pixels in CGB format
uint16_t pixels[RES_Y][RES_X];

const uint16_t gpu::MONOCHROME_COLORS[] = { 0x7FFF, 0x5294, 0x294A, 0x0 };

inline static void CopyPixelsToTexture()
{
	void* textureData;
	int texturePitch;
	SDL_LockTexture(gpu::outTexture, nullptr, &textureData, &texturePitch);
	
	for (int y = 0; y < RES_Y; y++)
	{
		uint32_t* surfaceRowPtr = reinterpret_cast<uint32_t*>(static_cast<char*>(textureData) + texturePitch * y);
		
		for (int x = 0; x < RES_X; x++)
		{
			surfaceRowPtr[x] = gpu::ToColor32(pixels[y][x]);
		}
	}
	
	SDL_UnlockTexture(gpu::outTexture);
}

void gpu::RunOneFrame()
{
	memset(pixels, 0, sizeof(pixels));
	
	RegisterState regCpy;
	
	auto MaybeTriggerStatInterrupt = [&] (uint8_t controlMask)
	{
		if (regCpy.stat & controlMask)
		{
			QueueInterrupt(INT_LCD_STAT);
		}
	};
	
	auto startTime = std::chrono::high_resolution_clock::now();
	
	for (int y = 0; y < RES_Y; y++)
	{
		std::bitset<RES_X> pixelHasBkgSprite;
		Sprite sprites[10];
		int numSprites = 0;
		
		{
			std::lock_guard<std::mutex> lock(regMutex);
			regCpy = reg;
		}
		
		if (!(regCpy.lcdc & (1 << 7)))
		{
			std::fill_n(pixels[0], RES_X * RES_Y, 3);
			CopyPixelsToTexture();
			return;
		}
		
		const bool renderSprites = regCpy.lcdc & 2;
		bool renderBackground = regCpy.lcdc & 1;
		const bool renderWindow = regCpy.lcdc & (1 << 5);
		const bool tileMode8000 = regCpy.lcdc & (1 << 4);
		uint8_t spriteFlagsMask = 0xFF;
		
		if (cgbMode && !renderBackground)
		{
			spriteFlagsMask = (uint8_t)(~SPF_BACKGROUND);
			renderBackground = true;
		}
		
		uint32_t bTileOffset = ((regCpy.lcdc & (1 << 3)) ? 0x1C00 : 0x1800);
		uint32_t wTileOffset = ((regCpy.lcdc & (1 << 6)) ? 0x1C00 : 0x1800);
		
		const uint8_t* bTileMap     = mem::vram[0] + bTileOffset;
		const uint8_t* bTileAttrMap = mem::vram[1] + bTileOffset;
		const uint8_t* wTileMap     = mem::vram[0] + wTileOffset;
		const uint8_t* wTileAttrMap = mem::vram[1] + wTileOffset;
		
		SetGPUMode(2, y);
		std::unique_lock<std::mutex> oamLock(mem::oamMutex);
		
		MaybeTriggerStatInterrupt(1 << 5);
		if (regCpy.lyc == y)
			MaybeTriggerStatInterrupt(1 << 6);
		
		//Sprites collect phase
		if (renderSprites)
		{
			const bool tallSprites = (regCpy.lcdc & 4);
			const int spriteMinY = y - (tallSprites ? 16 : 8);
			
			for (int i = 0; i < 40 && numSprites < 10; i++)
			{
				int spy = (int)mem::oam[i * 4 + 0] - 16;
				int spx = (int)mem::oam[i * 4 + 1] - 8;
				if (spx > -8 && spx < RES_X && spy > spriteMinY && spy <= y)
				{
					uint8_t tile = mem::oam[i * 4 + 2];
					uint8_t flags = mem::oam[i * 4 + 3];
					
					//Shifts tall sprites
					if (tallSprites)
					{
						if ((spy > y - 8) != (bool)(flags & SPF_FLIP_Y))
							tile &= 0xFE; //Use top tile
						else
							tile |= 0x1; //Use bottom tile
					}
					
					int r;
					if (flags & SPF_FLIP_Y)
						r = spy - spriteMinY - 1;
					else
						r = y - spy;
					
					sprites[numSprites].x = spx;
					sprites[numSprites].row = (uint8_t)r % 8;
					sprites[numSprites].tile = tile;
					sprites[numSprites].flags = flags & spriteFlagsMask;
					sprites[numSprites].palette = (flags & SPF_PALETTE1) ? regCpy.obp1 : regCpy.obp0;
					
					numSprites++;
				}
			}
			
			//Sorts sprites to have correct priority
			if (!cgbMode)
			{
				std::stable_sort(sprites, sprites + numSprites, [&] (const Sprite& a, const Sprite& b)
				{
					return a.x < b.x;
				});
			}
		}
		
		std::this_thread::sleep_until(startTime + std::chrono::nanoseconds(MODE_2_END_NS));
		
		SetGPUMode(3, y);
		std::unique_lock<std::mutex> vramLock(mem::vramMutex);
		
		//Renders background sprites
		if (renderSprites)
		{
			for (int s = numSprites - 1; s >= 0; s--)
			{
				if (!(sprites[s].flags & SPF_BACKGROUND))
					continue;
				
				for (int x = 0; x < 8; x++)
				{
					int dst = x + sprites[s].x;
					if (dst >= 0 && dst < RES_X)
					{
						pixels[y][dst] = SampleSprite(sprites[s], x).second;
						pixelHasBkgSprite.set(dst);
					}
				}
			}
		}
		
		constexpr uint8_t BGATTR_FLIP_X = 1 << 5;
		constexpr uint8_t BGATTR_FLIP_Y = 1 << 6;
		constexpr uint8_t BGATTR_HIGH_PRIORITY = 1 << 7;
		
		auto RenderBackPixel = [&] (uint8_t tileIdx, uint8_t tileAttr, uint32_t dstX, uint32_t srcX, uint32_t srcY)
		{
			Tile* tileMem = reinterpret_cast<Tile*>(mem::vram[(tileAttr >> 3) & 1]);
			Tile& tile = tileMode8000 ? tileMem[tileIdx] : tileMem[256 + (int8_t)tileIdx];
			
			uint32_t px = srcX % 8;
			px = (tileAttr & BGATTR_FLIP_X) ? (7 - px) : px;
			
			uint32_t py = srcY % 8;
			py = (tileAttr & BGATTR_FLIP_Y) ? (7 - py) : py;
			
			uint8_t color = tile.At(px, py);
			if (color != 0 || !pixelHasBkgSprite[dstX] || (tileAttr & BGATTR_HIGH_PRIORITY))
			{
				if (cgbMode)
				{
					pixels[y][dstX] = ResolveCGBColor(mem::backPaletteMemory, tileAttr & 7, color);
				}
				else
				{
					pixels[y][dstX] = gpu::ResolveColorMonochrome(color, regCpy.bgp);
				}
			}
		};
		
		//Renders the background
		if (renderBackground)
		{
			const uint32_t srcY = (y + regCpy.scy) % 256;
			const uint32_t tileMapOffset = (srcY / 8) * 32;
			for (uint32_t dstX = 0; dstX < RES_X; dstX++)
			{
				const uint32_t srcX = (dstX + regCpy.scx) % 256;
				const uint32_t tileMapIdx = tileMapOffset + srcX / 8;
				const uint8_t tileIdx = bTileMap[tileMapIdx];
				const uint8_t tileAttr = bTileAttrMap[tileMapIdx];
				RenderBackPixel(tileIdx, tileAttr, dstX, srcX, srcY);
			}
		}
		
		//Renders the window
		if (renderWindow && y >= regCpy.wy)
		{
			const uint32_t srcY = y - regCpy.wy;
			const int wx = regCpy.wx - 7;
			const uint32_t tileMapOffset = (srcY / 8) * 32;
			for (uint32_t dstX = std::max(wx, 0); dstX < RES_X; dstX++)
			{
				const uint32_t srcX = dstX - wx;
				const uint32_t tileMapIdx = tileMapOffset + srcX / 8;
				const uint8_t tileIdx = wTileMap[tileMapIdx];
				const uint8_t tileAttr = wTileAttrMap[tileMapIdx];
				RenderBackPixel(tileIdx, tileAttr, dstX, srcX, srcY);
			}
		}
		
		//Renders foreground sprites
		if (renderSprites)
		{
			for (int s = numSprites - 1; s >= 0; s--)
			{
				if (sprites[s].flags & SPF_BACKGROUND)
					continue;
				
				for (int x = 0; x < 8; x++)
				{
					int dst = x + sprites[s].x;
					if (dst >= 0 && dst < RES_X)
					{
						auto [transparent, color] = SampleSprite(sprites[s], x);
						if (!transparent)
							pixels[y][dst] = color;
					}
				}
			}
		}
		
		if (y == RES_Y - 1)
		{
			memcpy(prevOAM, mem::oam, sizeof(mem::oam));
		}
		
		oamLock.unlock();
		vramLock.unlock();
		
		std::this_thread::sleep_until(startTime + std::chrono::nanoseconds(MODE_3_END_NS));
		
		SetGPUMode(0, y);
		MaybeTriggerStatInterrupt(1 << 3);
		
		auto endTime = startTime + std::chrono::nanoseconds(MODE_0_END_NS);
		std::this_thread::sleep_until(endTime);
		startTime = endTime;
	}
	
	SetGPUMode(1, RES_Y);
	QueueInterrupt(INT_VBLANK);
	MaybeTriggerStatInterrupt(1 << 4);
	
	for (int y = RES_Y; y <= 153; y++)
	{
		SetGPUMode(1, y);
		std::this_thread::sleep_for(std::chrono::nanoseconds(MODE_0_END_NS));
	}
	
	CopyPixelsToTexture();
}
