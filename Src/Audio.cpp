#include "Audio.hpp"
#include "Memory.hpp"
#include "Common.hpp"

#include <SDL.h>
#include <cassert>
#include <iostream>
#include <vector>
#include <queue>

constexpr uint32_t CLOCKS_PER_SAMPLE = 64;
constexpr uint32_t OUTPUT_FREQ = 60000;
constexpr uint32_t SEQUENCER_FREQ = 512;
constexpr uint32_t C1_C2_FREQ = 131072;
constexpr uint32_t C3_FREQ = 65536;
constexpr uint32_t SAMPLES_PER_PUSH = 4096;

static const int SQUARE_WAVE_PATTERNS[4][8] = 
{
	{ -1, 1, 1, 1, 1, 1, 1, 1 },
	{ -1, -1, 1, 1, 1, 1, 1, 1 },
	{ -1, -1, -1, -1, 1, 1, 1, 1 },
	{ -1, -1, -1, -1, -1, -1, 1, 1 }
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
	if (audioDeviceId == 0 || realAudioSpec.freq != OUTPUT_FREQ)
	{
		std::cout << SDL_GetError() << std::endl;
		return;
	}
	
	SDL_PauseAudioDevice(audioDeviceId, 0);
}

static uint32_t generatedSamples;

struct ChannelData
{
	uint32_t originalFreq = 0;
	uint32_t freq = 0;
	uint32_t vol = 0;
	uint32_t volSweepTimer = 0;
	uint32_t timer = 0;
	uint32_t pos = 0;
	uint32_t elapsedLength = 0;
	uint32_t lengthTarget = 0;
	double sampleSumL = 0;
	double sampleSumR = 0;
	bool onFlag;
};

ChannelData channel1;
ChannelData channel2;
ChannelData channel3;
ChannelData* channels[] = { nullptr, &channel1, &channel2, &channel3 };

uint32_t channel1FreqSweepSteps = 0;

uint32_t seqStep;
uint32_t seqTimer;

static std::vector<int8_t> pendingSamples;

void ResetAudioChannel(int channel)
{
	channels[channel]->onFlag = true;
	channels[channel]->timer = 1;
	channels[channel]->volSweepTimer = 0;
	channels[channel]->elapsedLength = 0;
	
	if (channel == 1)
		channel1FreqSweepSteps = 0;
}

void SetAudioVolume(int channel, uint32_t vol)
{
	channels[channel]->vol = vol;
	channels[channel]->volSweepTimer = 0;
}

void SetAudioFrequency(int channel, uint32_t freq)
{
	channels[channel]->freq = channels[channel]->originalFreq = freq;
}

void SetAudioChannelLen(int channel, uint32_t length)
{
	if (channel == 3)
		channels[channel]->lengthTarget = 256 - length;
	else
		channels[channel]->lengthTarget = 64 - length;
}

uint8_t GetRegisterNR52()
{
	return ((uint8_t)channel1.onFlag) | ((uint8_t)channel2.onFlag << 1) | ((uint8_t)channel3.onFlag << 2);
}

inline void UpdateChannelElapsed(ChannelData& channel, int enableReg)
{
	if (channel.elapsedLength < channel.lengthTarget && (ioReg[enableReg] & (1 << 6)))
	{
		channel.elapsedLength++;
		if (channel.elapsedLength == channel.lengthTarget)
			channel.onFlag = false;
	}
}

inline void UpdateChannelVolume(ChannelData& channel, int reg)
{
	channel.volSweepTimer++;
	uint32_t sweepTime = ioReg[reg] & 7;
	if (channel.volSweepTimer >= sweepTime && sweepTime != 0)
	{
		channel.volSweepTimer = 0;
		
		bool subtract = ioReg[reg] & (1 << 3);
		if (subtract && channel.vol > 0)
			channel.vol--;
		else if (!subtract && channel.vol < 15)
			channel.vol++;
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
		channel1.pos = 0;
		channel2.pos = 0;
		channel3.pos = 0;
		channel1.onFlag = false;
		channel2.onFlag = false;
		channel3.onFlag = false;
		return;
	}
	
	uint8_t channelPan = ioReg[IOREG_NR51];
	
	double volL = (double)((ioReg[IOREG_NR50] >> 4) & 7) / 7.0;
	double volR = (double)(ioReg[IOREG_NR50] & 7) / 7.0;
	
	//Channel 1
	if (channel1.onFlag && channel1.vol > 0 && channel1.freq < 2048)
	{
		const uint32_t c1Pattern = ioReg[IOREG_NR11] >> 6;
		const double c1Vol = SQUARE_WAVE_PATTERNS[c1Pattern][channel1.pos] * (int)channel1.vol;
		if (channelPan & CPAN_1L)
			channel1.sampleSumL += c1Vol * volL;
		if (channelPan & CPAN_1R)
			channel1.sampleSumR += c1Vol * volR;
	}
	
	//Channel 2
	if (channel2.onFlag && channel2.vol > 0)
	{
		const uint32_t c2Pattern = ioReg[IOREG_NR21] >> 6;
		const double c2Vol = SQUARE_WAVE_PATTERNS[c2Pattern][channel2.pos] * (int)channel2.vol;
		if (channelPan & CPAN_2L)
			channel2.sampleSumL += c2Vol * volL;
		if (channelPan & CPAN_2R)
			channel2.sampleSumR += c2Vol * volR;
	}
	
	//Channel 3
	if ((ioReg[IOREG_NR30] & (1 << 7)) && channel3.onFlag)
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
		
		sampleL += channel1.sampleSumL * (MAX_PER_CHANNEL / (CLOCKS_PER_SAMPLE * 16.0));
		sampleR += channel1.sampleSumR * (MAX_PER_CHANNEL / (CLOCKS_PER_SAMPLE * 16.0));
		
		sampleL += channel2.sampleSumL * (MAX_PER_CHANNEL / (CLOCKS_PER_SAMPLE * 16.0));
		sampleR += channel2.sampleSumR * (MAX_PER_CHANNEL / (CLOCKS_PER_SAMPLE * 16.0));
		
		sampleL += channel3.sampleSumL * (MAX_PER_CHANNEL / (CLOCKS_PER_SAMPLE * 8)) - MAX_PER_CHANNEL / 2;
		sampleR += channel3.sampleSumR * (MAX_PER_CHANNEL / (CLOCKS_PER_SAMPLE * 8)) - MAX_PER_CHANNEL / 2;
		
		pendingSamples.push_back(std::max(std::min((int)std::round(sampleL), 127), -127));
		pendingSamples.push_back(std::max(std::min((int)std::round(sampleR), 127), -127));
		
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
		
		if (seqStep == 2 || seqStep == 6)
		{
			uint32_t sweepTime = (ioReg[IOREG_NR10] >> 4) & 7;
			uint32_t shift = ioReg[IOREG_NR10] & 7;
			if (sweepTime != 0)
			{
				if (channel1FreqSweepSteps++ == sweepTime)
				{
					channel1FreqSweepSteps = 0;
					int deltaFreq = channel1.freq >> shift;
					channel1.freq = std::max(std::min((int)channel1.freq + ((ioReg[IOREG_NR10] & (1 << 3)) ? -deltaFreq : deltaFreq), 2048), 0);
				}
			}
		}
		
		if (seqStep % 2 == 0)
		{
			UpdateChannelElapsed(channel1, IOREG_NR14);
			UpdateChannelElapsed(channel2, IOREG_NR24);
			UpdateChannelElapsed(channel3, IOREG_NR34);
		}
		
		if (seqStep == 7)
		{
			UpdateChannelVolume(channel1, IOREG_NR12);
			UpdateChannelVolume(channel2, IOREG_NR22);
		}
		
		seqStep = (seqStep + 1) % 8;
	}
	
	if (channel1.timer-- == 0)
	{
		channel1.timer = (CLOCK_RATE / (C1_C2_FREQ * 8)) * (2048 - channel1.freq);
		channel1.pos = (channel1.pos + 1) % 8;
	}
	
	if (channel2.timer-- == 0)
	{
		channel2.timer = (CLOCK_RATE / (C1_C2_FREQ * 8)) * (2048 - channel2.freq);
		channel2.pos = (channel2.pos + 1) % 8;
	}
	
	if (channel3.timer-- == 0)
	{
		channel3.timer = (CLOCK_RATE / (C3_FREQ * 32)) * (2048 - channel3.freq);
		channel3.pos = (channel3.pos + 1) % 32;
	}
}
