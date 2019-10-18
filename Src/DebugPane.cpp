#include "DebugPane.hpp"
#include "GPU.hpp"
#include "Memory.hpp"
#include "Input.hpp"
#include "CPU.hpp"
#include "../Font.h"

#include <sstream>
#include <iomanip>

DebugPane* DebugPane::instance;

const char* wantedChars = "ABCDEFGHIJKLMNOQRSTUVWXYZ0123456789:- ";

DebugPane::DebugPane(SDL_Renderer* renderer)
{
	m_font16 = TTF_OpenFontRW(SDL_RWFromConstMem(font_ttf, font_ttf_len), 1, 16);
	m_font12 = TTF_OpenFontRW(SDL_RWFromConstMem(font_ttf, font_ttf_len), 1, 12);
	
	m_tilesTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, 32 * 8, 48 * 8);
}

DebugPane::~DebugPane()
{
	TTF_CloseFont(m_font16);
	TTF_CloseFont(m_font12);
}

void DebugPane::HandleEvent(SDL_Event& event)
{
	if (event.type == SDL_KEYDOWN)
	{
		if (event.key.keysym.scancode == SDL_SCANCODE_F1)
			m_spriteOverlayEnabled = !m_spriteOverlayEnabled;
	}
}

void DebugPane::UpdateTilesTexture(SDL_Texture* texture, TilePalette palette)
{
	constexpr uint8_t PALETTE = 0xE4;
	
	void* pixels;
	int pitch;
	SDL_LockTexture(texture, nullptr, &pixels, &pitch);
	
	const gpu::Tile* tiles = reinterpret_cast<gpu::Tile*>(mem::vram);
	for (int ty = 0; ty < 48; ty++)
	{
		for (int py = 0; py < 8; py++)
		{
			uint32_t* rowBegin = reinterpret_cast<uint32_t*>(static_cast<char*>(pixels) + pitch * (ty * 8 + py));
			
			for (int tx = 0; tx < 32; tx++)
			{
				for (int px = 0; px < 8; px++)
				{
					const int dstX = tx * 8 + px;
					rowBegin[dstX] = gpu::ToColor32(gpu::ResolveColorMonochrome(tiles[ty * 32 + tx].At(px, py), PALETTE));
				}
			}
		}
	}
	
	SDL_UnlockTexture(texture);
}

void DebugPane::Draw(SDL_Renderer* renderer)
{
	constexpr int START_X = RES_X * PIXEL_SCALE + BORDER_WIDTH;
	const SDL_Color backColor = { 45, 66, 85, 255 };
	const SDL_Color textColor = { 255, 255, 255, 255 };
	
	UpdateTilesTexture(m_tilesTexture, m_tilePalette);
	
	SDL_Rect borderRect = { START_X - BORDER_WIDTH, 0, BORDER_WIDTH, RES_Y * PIXEL_SCALE };
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderFillRect(renderer, &borderRect);
	
	SDL_Rect paneRect = { START_X, 0, WIDTH - BORDER_WIDTH, RES_Y * PIXEL_SCALE };
	SDL_SetRenderDrawColor(renderer, backColor.r, backColor.g, backColor.b, backColor.a);
	SDL_RenderFillRect(renderer, &paneRect);
	
	//Renders tiles
	SDL_Rect tilesDst = { START_X, 0, 32 * 8, 48 * 8 };
	SDL_RenderCopy(renderer, m_tilesTexture, nullptr, &tilesDst);
	
	gpu::RegisterState gpuReg;
	{
		std::lock_guard<std::mutex> lock(gpu::regMutex);
		gpuReg = gpu::reg;
	}
	
	const uint32_t buttonMask = GetButtonMask();
	
	const uint32_t mbcNames[] = { 1, 2, 5 };
	
	const uint32_t regValues[] =
	{
		gpuReg.lyc, gpuReg.lcdc,
		gpuReg.scx, gpuReg.scy,
		gpuReg.wx, gpuReg.wy,
		gpuReg.obp0, gpuReg.obp1,
		gpuReg.bgp, ioReg[IOREG_TIMA],
		ioReg[IOREG_TMA], ioReg[IOREG_TAC],
		cpu.intEnableReg, cpu.intEnableMaster,
		buttonMask, cpu.halted,
		cgbMode, mbcNames[(int)mem::activeMBC],
		cpu.reg8[REG_A], cpu.reg8[REG_F],
		cpu.reg8[REG_B], cpu.reg8[REG_C],
		cpu.reg8[REG_D], cpu.reg8[REG_E],
		cpu.reg8[REG_H], cpu.reg8[REG_L]
	};
	
	const char* regNames[] = 
	{
		"LYC", "LCDC",
		"SCX", "SCY",
		"WX", "WY",
		"OBP0", "OBP1",
		"BGP", "TIMA",
		"TMA", "TAC",
		"IE", "IME",
		"BTN", "HLT",
		"CGB", "MBC",
		"RegA", "RegF",
		"RegB", "RegC",
		"RegD", "RegE",
		"RegH", "RegL",
	};
	
	std::ostringstream textStream;
	textStream << std::hex << std::setfill('0');
	
	size_t regValI = 0;
	for (size_t regNameI = 0; regNameI < std::size(regNames); regNameI++)
	{
		if (regNames[regNameI])
		{
			for (int i = strlen(regNames[regNameI]); i < 4; i++)
				textStream << ' ';
			textStream << regNames[regNameI] << ": " << std::setw(2) << regValues[regValI];
			regValI++;
		}
		
		if (regNameI % 2)
			textStream << "\n";
		else
			textStream << "  ";
	}
	
	textStream << " PC: " << std::setw(4) << cpu.pc << "\n";
	textStream << " SP: " << std::setw(4) << cpu.sp << "\n\n";
	textStream << "CPU: " << std::dec << std::fixed << std::setprecision(2) << (m_procTimeSum / (double)CLOCK_RATE) << "/" << NSPerClockCycle() << " ns\n";
	textStream << "GPU: " << std::dec << std::fixed << std::setprecision(2) << (m_gpuTime / 1E6) << " ms\n";
	textStream << "FPS: " << std::dec << m_fps << " Hz";
	
	std::string textStr = textStream.str();
	
	SDL_Surface* textSurface = TTF_RenderUTF8_Blended_Wrapped(m_font16, textStr.c_str(), textColor, 256);
	SDL_Texture* textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
	
	SDL_Rect textDst = { START_X + tilesDst.w + 20, 0, textSurface->w, textSurface->h };
	SDL_RenderCopy(renderer, textTexture, nullptr, &textDst);
	
	SDL_DestroyTexture(textTexture);
	SDL_FreeSurface(textSurface);
	
	if (m_spriteOverlayEnabled)
	{
		DrawSpriteOverlay(renderer);
	}
}

void DebugPane::DrawSpriteOverlay(SDL_Renderer* renderer) const
{
	const SDL_Color backColor = { 87, 16, 7, 200 };
	const SDL_Color textColor = { 250, 150, 150, 255 };
	
	for (int i = 0; i < 40; i++)
	{
		int spy = (int)gpu::prevOAM[i * 4 + 0] - 16;
		int spx = (int)gpu::prevOAM[i * 4 + 1] - 8;
		if (spx > -8 && spx < RES_X && spy > -16 && spy < RES_Y)
		{
			uint8_t tile = gpu::prevOAM[i * 4 + 2];
			uint8_t flags = gpu::prevOAM[i * 4 + 3];
			
			SDL_Rect spriteRect = { spx * PIXEL_SCALE, spy * PIXEL_SCALE, 8 * PIXEL_SCALE, 8 * PIXEL_SCALE };
			
			SDL_SetRenderDrawColor(renderer, backColor.r, backColor.g, backColor.b, backColor.a);
			SDL_RenderFillRect(renderer, &spriteRect);
			
			SDL_SetRenderDrawColor(renderer, textColor.r, textColor.g, textColor.b, textColor.a);
			SDL_RenderDrawRect(renderer, &spriteRect);
			
			char textBuffer[32];
			snprintf(textBuffer, sizeof(textBuffer), "F:%02x\nT:%02x", flags, tile);
			
			SDL_Surface* textSurface = TTF_RenderUTF8_Blended_Wrapped(m_font12, textBuffer, textColor, 256);
			SDL_Texture* textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
			
			SDL_Rect textDst;
			textDst.x = spriteRect.x + 2;
			textDst.y = spriteRect.y + 1;
			textDst.w = textSurface->w;
			textDst.h = textSurface->h;
			SDL_RenderCopy(renderer, textTexture, nullptr, &textDst);
			
			SDL_DestroyTexture(textTexture);
			SDL_FreeSurface(textSurface);
		}
	}
}
