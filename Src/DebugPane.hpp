#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <atomic>

class DebugPane
{
public:
	DebugPane(SDL_Renderer* renderer);
	~DebugPane();
	
	void HandleEvent(SDL_Event& event);
	
	void Draw(SDL_Renderer* renderer);
	
	void SetProcTimeSum(int64_t val)
	{
		m_procTimeSum = val;
	}
	
	void SetGPUTime(int64_t val)
	{
		m_gpuTime = val;
	}
	
	void SetFPS(int fps)
	{
		m_fps = fps;
	}
	
	static constexpr uint32_t BORDER_WIDTH = 8;
	static constexpr uint32_t WIDTH = 512 + BORDER_WIDTH;
	
	static DebugPane* instance;
	
private:
	enum class TilePalette
	{
		BGP,
		OBP0,
		OBP1
	};
	
	void DrawSpriteOverlay(SDL_Renderer* renderer) const;
	
	static void UpdateTilesTexture(SDL_Texture* texture, TilePalette palette);
	
	SDL_Texture* m_tilesTexture;
	TilePalette m_tilePalette = TilePalette::BGP;
	
	bool m_spriteOverlayEnabled = false;
	
	int m_fps = 0;
	
	TTF_Font* m_font16;
	TTF_Font* m_font12;
	
	uint64_t m_gpuTime = 0;
	std::atomic_int64_t m_procTimeSum { 0 };
};
