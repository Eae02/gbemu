#include "Audio.hpp"
#include "Memory.hpp"
#include "Common.hpp"

#include <SDL.h>
#include <cassert>
#include <iostream>
#include <vector>
#include <queue>

constexpr uint32_t CLOCKS_PER_SAMPLE = 64;
constexpr uint32_t OUTPUT_FREQ = CLOCK_RATE / CLOCKS_PER_SAMPLE;
constexpr uint32_t SEQUENCER_FREQ = 512;
constexpr uint32_t C1_C2_FREQ = 131072;
constexpr uint32_t C3_FREQ = 65536;
constexpr uint32_t SAMPLES_PER_PUSH = 4096;

static const int8_t SQUARE_WAVE_PATTERNS[4][8] = 
{
	-1, 1, 1, 1, 1, 1, 1, 1,
	-1, -1, 1, 1, 1, 1, 1, 1,
	-1, -1, -1, -1, 1, 1, 1, 1,
	-1, -1, -1, -1, -1, -1, 1, 1,
};

bool audioActive = false;
int audioDeviceId;

void InitAudio()
{
	SDL_AudioSpec audioSpec = { };
	audioSpec.freq = OUTPUT_FREQ;
	audioSpec.callback = nullptr;
	audioSpec.channels = 2;
	audioSpec.samples = SAMPLES_PER_PUSH;
	audioSpec.format = AUDIO_S8;
	
	SDL_AudioSpec realAudioSpec;
	audioDeviceId = SDL_OpenAudioDevice(nullptr, 0, &audioSpec, &realAudioSpec, 0);
	if (audioDeviceId == 0)
	{
		std::cout << SDL_GetError() << std::endl;
		return;
	}
	
	SDL_PauseAudioDevice(audioDeviceId, 0);
}

static uint32_t generatedSamples;

struct ChannelData
{
	uint32_t timer = 0;
	uint32_t pos = 0;
	uint32_t elapsed = 0;
	double sampleSumL = 0;
	double sampleSumR = 0;
};

ChannelData channel1;
ChannelData channel2;
ChannelData channel3;

int channel1FreqSweep = 0;

uint32_t seqStep;
uint32_t seqTimer;

static std::vector<int8_t> pendingSamples;

template <uint32_t CHANNEL_FREQ, uint32_t IOREG_LO>
uint32_t GetTimerStart()
{
	uint32_t freq = (uint32_t)ioReg[IOREG_LO] | ((uint32_t)(ioReg[IOREG_LO + 1] & 7) << 8);
	return (CLOCK_RATE / CHANNEL_FREQ) * (2048 - freq);
}

void ResetAudioChannel(int channel)
{
	switch (channel)
	{
	case 1:
		channel1.pos = 0;
		channel1.timer = 1;
		channel1FreqSweep = 0;
		break;
	case 2:
		channel2.pos = 0;
		channel2.timer = 1;
		break;
	case 3:
		channel3.pos = 0;
		channel3.timer = 1;
		break;
	}
}

constexpr uint8_t CPAN_4L = 1 << 7;
constexpr uint8_t CPAN_3L = 1 << 6;
constexpr uint8_t CPAN_2L = 1 << 5;
constexpr uint8_t CPAN_1L = 1 << 4;
constexpr uint8_t CPAN_4R = 1 << 3;
constexpr uint8_t CPAN_3R = 1 << 2;
constexpr uint8_t CPAN_2R = 1 << 1;
constexpr uint8_t CPAN_1R = 1 << 0;

void UpdateAudio()
{
	if (!(ioReg[IOREG_NR52] & (1 << 7)) || audioDeviceId == 0)
	{
		return;
	}
	
	if (SDL_GetQueuedAudioSize(audioDeviceId) < 1024)
	{
		SDL_PauseAudioDevice(audioDeviceId, true);
	}
	
	if (SDL_GetQueuedAudioSize(audioDeviceId) < 4096)
	{
		SDL_PauseAudioDevice(audioDeviceId, false);
	}
	
	uint8_t channelPan = ioReg[IOREG_NR51];
	
	double volL = (double)((ioReg[IOREG_NR50] >> 4) & 7) / 7.0;
	double volR = (double)(ioReg[IOREG_NR50] & 7) / 7.0;
	
	//Channel 1
	const uint32_t c1Pattern = ioReg[IOREG_NR11] >> 6;
	const double c1Vol = SQUARE_WAVE_PATTERNS[c1Pattern][channel1.pos] * (ioReg[IOREG_NR12] >> 4) / 16.0;
	if (channelPan & CPAN_1L)
		channel1.sampleSumL += c1Vol;
	if (channelPan & CPAN_1R)
		channel1.sampleSumR += c1Vol;
	
	//Channel 2
	const uint32_t c2Pattern = ioReg[IOREG_NR21] >> 6;
	const double c2Vol = SQUARE_WAVE_PATTERNS[c2Pattern][channel2.pos] * (ioReg[IOREG_NR22] >> 4) / 16.0;
	if (channelPan & CPAN_2L)
		channel2.sampleSumL += c2Vol;
	if (channelPan & CPAN_2R)
		channel2.sampleSumR += c2Vol;
	
	//Channel 3
	if (ioReg[IOREG_NR30] & (1 << 7))
	{
		uint32_t c3Volume = (ioReg[IOREG_NR32] >> 5) & 3;
		if (c3Volume != 0)
		{
			uint8_t c3Sample = (ioReg[0x30 + channel3.pos / 2] >> ((channel3.pos % 2) ? 0 : 4)) & 0xF;
			c3Sample >>= (c3Volume - 1);
			if (channelPan & CPAN_3L)
				channel3.sampleSumL += c3Sample * volL;
			if (channelPan & CPAN_3R)
				channel3.sampleSumR += c3Sample * volR;
		}
	}
	
	generatedSamples++;
	if (generatedSamples == CLOCKS_PER_SAMPLE)
	{
		double sampleL = 0;
		double sampleR = 0;
		
		constexpr double MAX_PER_CHANNEL = 127.0 / 3.0;
		
		sampleL += channel1.sampleSumL * (MAX_PER_CHANNEL / CLOCKS_PER_SAMPLE);
		sampleR += channel1.sampleSumR * (MAX_PER_CHANNEL / CLOCKS_PER_SAMPLE);
		
		sampleL += channel2.sampleSumL * (MAX_PER_CHANNEL / CLOCKS_PER_SAMPLE);
		sampleR += channel2.sampleSumR * (MAX_PER_CHANNEL / CLOCKS_PER_SAMPLE);
		
		sampleL += channel3.sampleSumL * (MAX_PER_CHANNEL / (CLOCKS_PER_SAMPLE * 8)) - 1;
		sampleR += channel3.sampleSumR * (MAX_PER_CHANNEL / (CLOCKS_PER_SAMPLE * 8)) - 1;
		
		pendingSamples.push_back(sampleL);
		pendingSamples.push_back(sampleR);
		
		if (pendingSamples.size() >= SAMPLES_PER_PUSH * 2)
		{
			if (SDL_QueueAudio(audioDeviceId, pendingSamples.data(), pendingSamples.size()))
			{
				std::cerr << "Failed to queue audio!\n";
			}
			pendingSamples.clear();
		}
		
		generatedSamples = 0;
		channel1.sampleSumL = 0;
		channel1.sampleSumR = 0;
		channel2.sampleSumL = 0;
		channel2.sampleSumR = 0;
		channel3.sampleSumL = 0;
		channel3.sampleSumR = 0;
	}
	
	
	if (seqTimer-- == 0)
	{
		seqTimer = 8192;
		seqStep = (seqStep + 1) % 8;
		
		if (seqStep == 2 || seqStep == 6)
		{
			
		}
	}
	
	if (channel1.timer-- == 0)
	{
		channel1.timer = GetTimerStart<C1_C2_FREQ * 8, IOREG_NR13>();
		channel1.pos = (channel1.pos + 1) % 8;
	}
	
	if (channel2.timer-- == 0)
	{
		channel2.timer = GetTimerStart<C1_C2_FREQ * 8, IOREG_NR23>();
		channel2.pos = (channel2.pos + 1) % 8;
	}
	
	if (channel3.timer-- == 0)
	{
		channel3.timer = GetTimerStart<C3_FREQ * 32, IOREG_NR33>();
		channel3.pos = (channel3.pos + 1) % 32;
	}
}
